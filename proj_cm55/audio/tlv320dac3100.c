/*******************************************************************************
* File Name        : tlv320dac3100.c
*
* Description      : Portable TLV320DAC3100 register driver, see header.
*
*                    Power-up order (TI SLAS671 example script): reset ->
*                    clock tree (PLL first, then NDAC / MDAC) -> interface ->
*                    processing block -> page 1 output stage (de-pop,
*                    mixer routing, driver gains, power up drivers, analog
*                    volumes) -> page 0 DAC power -> volume -> unmute.
*
*******************************************************************************/
#include <stddef.h>

#include "tlv320dac3100.h"

/* Clock limits (TLV320DAC3100 datasheet) */
#define PLL_IN_MIN_HZ           (512000UL)      /* PLL_CLKIN / P, D = 0 */
#define PLL_IN_MAX_HZ           (20000000UL)
#define PLL_OUT_MIN_HZ          (80000000UL)
#define PLL_OUT_MAX_HZ          (110000000UL)
#define DAC_CLK_MAX_HZ          (49152000UL)
#define DAC_MOD_MIN_HZ          (2800000UL)     /* DOSR x fs, filter A */
#define DAC_MOD_MAX_HZ          (6200000UL)
#define PRB_RC_MIN              (12U)           /* MDAC x DOSR / 32 */

#define PRB_P1                  (1U)
#define PRB_P25                 (25U)           /* filter A, beep generator */

/* Fixed register values */
#define CLK_MUX_PLL             (0x03U) /* PLL_CLKIN = MCLK, CODEC_CLKIN = PLL */
#define CLK_MUX_MCLK            (0x00U) /* CODEC_CLKIN = MCLK */
#define IF_I2S_16BIT_SLAVE      (0x00U) /* I2S, 16 bit, BCLK and WCLK inputs */
#define HP_DRIVERS_OFF          (0x04U) /* common mode 1.35 V, drivers off */
#define HP_DRIVERS_ON           (0xC4U) /* HPL and HPR powered */
#define HP_POP_REMOVAL          (0x4EU) /* 1.22 s power-on, 3.9 ms ramp step */
#define OUT_MIXER_DAC           (0x44U) /* DAC_L -> left mixer, DAC_R -> right */
#define HP_DRV_0DB_UNMUTE       (0x06U)
#define HP_ANALOG_VOL           (0x8CU) /* routed, -6 dB */
#define SPK_ANALOG_VOL          (0x80U) /* routed, 0 dB */
#define SPK_AMP_ON              (0x86U)
#define SPK_AMP_OFF             (0x06U)
#define SPK_DRV_UNMUTE          (0x04U)
#define DAC_PATH_ON             (0xD4U) /* L+R DAC on, left->L, right->R */
#define DAC_PATH_OFF            (0x14U)
#define DAC_MUTE_BOTH           (0x0CU)
#define DAC_UNMUTE_BOTH         (0x00U)
#define BEEP_ENABLE             (0x80U)
#define BEEP_LEFT_MASTER        (0x40U)

#define RESET_WAIT_MS           (2U)
#define PLL_LOCK_WAIT_MS        (10U)


static int bus_write(tlv320_t* dev, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return (0 == dev->bus->write(dev->bus->ctx, TLV320_I2C_ADDR, buf, 2U)) ? TLV320_OK : TLV320_ERR_BUS;
}

static int select_page(tlv320_t* dev, uint8_t page)
{
    int rc;

    if (dev->page == (int8_t)page)
    {
        return TLV320_OK;
    }

    rc = bus_write(dev, TLV320_P0_PAGE, page);
    dev->page = (TLV320_OK == rc) ? (int8_t)page : (int8_t)-1;
    return rc;
}

static int wr(tlv320_t* dev, uint8_t page, uint8_t reg, uint8_t value)
{
    int rc = select_page(dev, page);
    return (TLV320_OK == rc) ? bus_write(dev, reg, value) : rc;
}

static int rd(tlv320_t* dev, uint8_t page, uint8_t reg, uint8_t* value)
{
    int rc = select_page(dev, page);

    if (TLV320_OK != rc)
    {
        return rc;
    }

    return (0 == dev->bus->read_reg(dev->bus->ctx, TLV320_I2C_ADDR, reg, value)) ? TLV320_OK : TLV320_ERR_BUS;
}

static void delay(tlv320_t* dev, uint32_t ms)
{
    if (NULL != dev->bus->delay_ms)
    {
        dev->bus->delay_ms(dev->bus->ctx, ms);
    }
}

/* Finds NDAC, MDAC, DOSR for codec_clkin -> fs. Returns true when found. */
static bool find_dividers(uint32_t codec_clkin, uint32_t fs, tlv320_clock_t* c)
{
    uint32_t dosr;

    /* Highest oversampling first: best image rejection */
    for (dosr = 1016U; dosr >= 8U; dosr -= 8U)
    {
        uint64_t mod = (uint64_t)dosr * fs;
        uint32_t nm, ndac;

        if ((mod < DAC_MOD_MIN_HZ) || (mod > DAC_MOD_MAX_HZ) || (0U != (codec_clkin % mod)))
        {
            continue;
        }

        nm = (uint32_t)(codec_clkin / mod);

        for (ndac = 1U; ndac <= 128U; ndac++)
        {
            uint32_t mdac;

            if ((0U != (nm % ndac)) || ((codec_clkin / ndac) > DAC_CLK_MAX_HZ))
            {
                continue;
            }

            mdac = nm / ndac;

            if ((mdac < 1U) || (mdac > 128U) || (((mdac * dosr) / 32U) < PRB_RC_MIN))
            {
                continue;
            }

            c->ndac = (uint8_t)ndac;
            c->mdac = (uint8_t)mdac;
            c->dosr = (uint16_t)dosr;
            c->codec_clkin_hz = codec_clkin;
            c->dac_clk_hz = codec_clkin / ndac;
            c->dac_mod_clk_hz = c->dac_clk_hz / mdac;
            c->fs_hz = c->dac_mod_clk_hz / dosr;
            return true;
        }
    }

    return false;
}


int tlv320_clock_calc(uint32_t mclk_hz, uint32_t fs_hz, bool use_pll, tlv320_clock_t* out)
{
    tlv320_clock_t c = { 0 };

    if ((NULL == out) || (0U == mclk_hz) || (0U == fs_hz))
    {
        return TLV320_ERR_PARAM;
    }

    if (use_pll)
    {
        uint32_t p, r, j;

        c.use_pll = true;
        c.prb = PRB_P25;

        /* R = 1 first (TI examples), then the lowest PLL frequency */
        for (r = 1U; r <= 4U; r++)
        {
            for (j = 4U; j <= 63U; j++)
            {
                for (p = 1U; p <= 8U; p++)
                {
                    uint64_t pll = ((uint64_t)mclk_hz * r * j) / p;
                    uint32_t pin = mclk_hz / p;

                    if ((0U != (((uint64_t)mclk_hz * r * j) % p)) ||
                        (pin < PLL_IN_MIN_HZ) || (pin > PLL_IN_MAX_HZ) ||
                        (pll < PLL_OUT_MIN_HZ) || (pll > PLL_OUT_MAX_HZ))
                    {
                        continue;
                    }

                    if (find_dividers((uint32_t)pll, fs_hz, &c))
                    {
                        c.pll_p = (uint8_t)p;
                        c.pll_r = (uint8_t)r;
                        c.pll_j = (uint8_t)j;
                        c.pll_d = 0U;
                        *out = c;
                        return TLV320_OK;
                    }
                }
            }
        }
        return TLV320_ERR_CLOCK;
    }

    c.use_pll = false;
    c.prb = PRB_P1;

    if (find_dividers(mclk_hz, fs_hz, &c))
    {
        *out = c;
        return TLV320_OK;
    }

    /* Fallback = Infineon audio-codec-tlv320dac3100 setting */
    if ((0U == (mclk_hz % fs_hz)) && ((mclk_hz / fs_hz) <= 1024U) && ((mclk_hz / fs_hz) >= 2U))
    {
        c.ndac = 1U;
        c.mdac = 1U;
        c.dosr = (uint16_t)(mclk_hz / fs_hz);
        c.codec_clkin_hz = mclk_hz;
        c.dac_clk_hz = mclk_hz;
        c.dac_mod_clk_hz = mclk_hz;
        c.fs_hz = fs_hz;
        *out = c;
        return TLV320_OK;
    }

    return TLV320_ERR_CLOCK;
}


static uint8_t spk_gain_bits(int gain_db)
{
    /* P1 R42 D4:D3: 00 = 6 dB, 01 = 12 dB, 10 = 18 dB, 11 = 24 dB */
    if (gain_db >= 24) { return 3U; }
    if (gain_db >= 18) { return 2U; }
    if (gain_db >= 12) { return 1U; }
    return 0U;
}

#define TRY(x)  do { int rc_ = (x); if (TLV320_OK != rc_) { return rc_; } } while (0)

int tlv320_init(tlv320_t* dev, const tlv320_bus_t* bus, const tlv320_clock_t* clock,
                uint8_t outputs, int spk_gain_db)
{
    const tlv320_clock_t* c = clock;

    if ((NULL == dev) || (NULL == bus) || (NULL == bus->write) || (NULL == bus->read_reg) ||
        (NULL == clock) || (0U == (outputs & (TLV320_OUT_SPEAKER | TLV320_OUT_HEADPHONE))))
    {
        return TLV320_ERR_PARAM;
    }

    dev->bus = bus;
    dev->clock = *clock;
    dev->outputs = outputs;
    dev->spk_drv = (uint8_t)((spk_gain_bits(spk_gain_db) << 3) | SPK_DRV_UNMUTE);
    dev->page = -1;
    dev->ready = false;

    /* Software reset (self clearing); the page register returns to 0 */
    TRY(wr(dev, 0U, TLV320_P0_SW_RESET, 0x01U));
    dev->page = 0;
    delay(dev, RESET_WAIT_MS);

    /* Clock tree */
    if (c->use_pll)
    {
        TRY(wr(dev, 0U, TLV320_P0_CLK_MUX, CLK_MUX_PLL));
        TRY(wr(dev, 0U, TLV320_P0_PLL_J, c->pll_j));
        TRY(wr(dev, 0U, TLV320_P0_PLL_D_MSB, (uint8_t)(c->pll_d >> 8)));
        TRY(wr(dev, 0U, TLV320_P0_PLL_D_LSB, (uint8_t)(c->pll_d & 0xFFU)));
        TRY(wr(dev, 0U, TLV320_P0_PLL_PR,
               (uint8_t)(0x80U | ((c->pll_p & 0x07U) << 4) | (c->pll_r & 0x0FU))));
        delay(dev, PLL_LOCK_WAIT_MS);
    }
    else
    {
        TRY(wr(dev, 0U, TLV320_P0_CLK_MUX, CLK_MUX_MCLK));
    }

    /* Divider value 128 is coded as 0 */
    TRY(wr(dev, 0U, TLV320_P0_NDAC, (uint8_t)(0x80U | (c->ndac & 0x7FU))));
    TRY(wr(dev, 0U, TLV320_P0_MDAC, (uint8_t)(0x80U | (c->mdac & 0x7FU))));
    TRY(wr(dev, 0U, TLV320_P0_DOSR_MSB, (uint8_t)((c->dosr >> 8) & 0x03U)));
    TRY(wr(dev, 0U, TLV320_P0_DOSR_LSB, (uint8_t)(c->dosr & 0xFFU)));

    /* Audio interface and processing block */
    TRY(wr(dev, 0U, TLV320_P0_IF_CTRL1, IF_I2S_16BIT_SLAVE));
    TRY(wr(dev, 0U, TLV320_P0_DATA_OFFSET, 0x00U));
    TRY(wr(dev, 0U, TLV320_P0_DAC_PRB, c->prb));
    TRY(wr(dev, 0U, TLV320_P0_VOL_PIN_CTRL, 0x00U));

    /* Output stage */
    TRY(wr(dev, 1U, TLV320_P1_HP_DRIVERS, HP_DRIVERS_OFF));
    TRY(wr(dev, 1U, TLV320_P1_HP_POP, HP_POP_REMOVAL));
    TRY(wr(dev, 1U, TLV320_P1_OUT_MIXER, OUT_MIXER_DAC));

    if (0U != (outputs & TLV320_OUT_HEADPHONE))
    {
        TRY(wr(dev, 1U, TLV320_P1_HPL_DRV, HP_DRV_0DB_UNMUTE));
        TRY(wr(dev, 1U, TLV320_P1_HPR_DRV, HP_DRV_0DB_UNMUTE));
        TRY(wr(dev, 1U, TLV320_P1_HP_DRIVERS, HP_DRIVERS_ON));
        TRY(wr(dev, 1U, TLV320_P1_HPL_VOL, HP_ANALOG_VOL));
        TRY(wr(dev, 1U, TLV320_P1_HPR_VOL, HP_ANALOG_VOL));
    }

    if (0U != (outputs & TLV320_OUT_SPEAKER))
    {
        TRY(wr(dev, 1U, TLV320_P1_SPK_DRV, dev->spk_drv));
        TRY(wr(dev, 1U, TLV320_P1_SPK_AMP, SPK_AMP_ON));
        TRY(wr(dev, 1U, TLV320_P1_SPK_VOL, SPK_ANALOG_VOL));
    }

    /* DAC channels on, muted until something is played */
    TRY(wr(dev, 0U, TLV320_P0_DAC_DATAPATH, DAC_PATH_ON));
    TRY(wr(dev, 0U, TLV320_P0_DAC_VOL_CTRL, DAC_MUTE_BOTH));
    TRY(wr(dev, 0U, TLV320_P0_DAC_LVOL, 0x00U));
    TRY(wr(dev, 0U, TLV320_P0_DAC_RVOL, 0x00U));

    dev->ready = true;
    return TLV320_OK;
}


int tlv320_set_volume_half_db(tlv320_t* dev, int half_db)
{
    uint8_t v;

    if ((NULL == dev) || !dev->ready)
    {
        return TLV320_ERR_STATE;
    }

    if (half_db < TLV320_VOL_MIN_HALF_DB) { half_db = TLV320_VOL_MIN_HALF_DB; }
    if (half_db > TLV320_VOL_MAX_HALF_DB) { half_db = TLV320_VOL_MAX_HALF_DB; }

    v = (uint8_t)(int8_t)half_db;   /* two's complement */
    TRY(wr(dev, 0U, TLV320_P0_DAC_LVOL, v));
    return wr(dev, 0U, TLV320_P0_DAC_RVOL, v);
}


int tlv320_mute(tlv320_t* dev, bool mute)
{
    if ((NULL == dev) || !dev->ready)
    {
        return TLV320_ERR_STATE;
    }

    return wr(dev, 0U, TLV320_P0_DAC_VOL_CTRL, mute ? DAC_MUTE_BOTH : DAC_UNMUTE_BOTH);
}


int tlv320_speaker_power(tlv320_t* dev, bool on)
{
    if ((NULL == dev) || !dev->ready)
    {
        return TLV320_ERR_STATE;
    }

    if (0U == (dev->outputs & TLV320_OUT_SPEAKER))
    {
        return TLV320_OK;
    }

    return wr(dev, 1U, TLV320_P1_SPK_AMP, on ? SPK_AMP_ON : SPK_AMP_OFF);
}


int tlv320_beep(tlv320_t* dev, int16_t sin_q15, int16_t cos_q15,
                uint32_t samples, uint8_t atten_db)
{
    if ((NULL == dev) || !dev->ready)
    {
        return TLV320_ERR_STATE;
    }

    if ((PRB_P25 != dev->clock.prb) || (0U == samples) || (samples > 0xFFFFFFUL))
    {
        return TLV320_ERR_PARAM;
    }

    if (atten_db > 61U)
    {
        atten_db = 61U;
    }

    TRY(wr(dev, 0U, TLV320_P0_BEEP_LEN_MSB, (uint8_t)(samples >> 16)));
    TRY(wr(dev, 0U, TLV320_P0_BEEP_LEN_MID, (uint8_t)(samples >> 8)));
    TRY(wr(dev, 0U, TLV320_P0_BEEP_LEN_LSB, (uint8_t)samples));
    TRY(wr(dev, 0U, TLV320_P0_BEEP_SIN_MSB, (uint8_t)((uint16_t)sin_q15 >> 8)));
    TRY(wr(dev, 0U, TLV320_P0_BEEP_SIN_LSB, (uint8_t)((uint16_t)sin_q15 & 0xFFU)));
    TRY(wr(dev, 0U, TLV320_P0_BEEP_COS_MSB, (uint8_t)((uint16_t)cos_q15 >> 8)));
    TRY(wr(dev, 0U, TLV320_P0_BEEP_COS_LSB, (uint8_t)((uint16_t)cos_q15 & 0xFFU)));
    TRY(wr(dev, 0U, TLV320_P0_BEEP_R, BEEP_LEFT_MASTER));
    /* Enable last: the bit self-clears when the beep has finished */
    return wr(dev, 0U, TLV320_P0_BEEP_L, (uint8_t)(BEEP_ENABLE | atten_db));
}


int tlv320_read_status(tlv320_t* dev, tlv320_status_t* status)
{
    uint8_t ot = 0xFFU, flag = 0U, sticky = 0U, spk = 0U;

    if ((NULL == dev) || (NULL == status) || (NULL == dev->bus))
    {
        return TLV320_ERR_PARAM;
    }

    TRY(rd(dev, 0U, TLV320_P0_OT_FLAG, &ot));
    TRY(rd(dev, 0U, TLV320_P0_DAC_FLAG1, &flag));
    TRY(rd(dev, 0U, TLV320_P0_IRQ_STICKY, &sticky));
    TRY(rd(dev, 1U, TLV320_P1_SPK_AMP, &spk));

    status->over_temperature = (0U == (ot & 0x02U));
    status->hp_short = (0U != (sticky & 0xC0U));
    status->spk_short = (0U != (spk & 0x01U));
    status->ldac_on = (0U != (flag & 0x80U));
    status->hpl_on  = (0U != (flag & 0x20U));
    status->spk_on  = (0U != (flag & 0x10U));
    status->rdac_on = (0U != (flag & 0x08U));
    status->hpr_on  = (0U != (flag & 0x02U));
    return TLV320_OK;
}


int tlv320_power_down(tlv320_t* dev)
{
    if ((NULL == dev) || !dev->ready)
    {
        return TLV320_ERR_STATE;
    }

    TRY(wr(dev, 0U, TLV320_P0_DAC_VOL_CTRL, DAC_MUTE_BOTH));
    TRY(wr(dev, 1U, TLV320_P1_SPK_AMP, SPK_AMP_OFF));
    TRY(wr(dev, 1U, TLV320_P1_HP_DRIVERS, HP_DRIVERS_OFF));
    TRY(wr(dev, 0U, TLV320_P0_DAC_DATAPATH, DAC_PATH_OFF));
    TRY(wr(dev, 0U, TLV320_P0_NDAC, (uint8_t)(dev->clock.ndac & 0x7FU)));
    TRY(wr(dev, 0U, TLV320_P0_MDAC, (uint8_t)(dev->clock.mdac & 0x7FU)));

    if (dev->clock.use_pll)
    {
        TRY(wr(dev, 0U, TLV320_P0_PLL_PR,
               (uint8_t)(((dev->clock.pll_p & 0x07U) << 4) | (dev->clock.pll_r & 0x0FU))));
    }

    dev->ready = false;
    return TLV320_OK;
}

/* [] END OF FILE */

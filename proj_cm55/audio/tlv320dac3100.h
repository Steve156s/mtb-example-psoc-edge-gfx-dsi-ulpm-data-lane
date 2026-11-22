/*******************************************************************************
* File Name        : tlv320dac3100.h
*
* Description      : Portable register-level driver for the TI TLV320DAC3100
*                    stereo audio DAC with headphone and mono Class-D speaker
*                    amplifier (I2C control, I2S audio, codec is I2S slave).
*                    No PSOC / RTOS dependency: the bus access is passed in
*                    as function pointers, so the driver is host-testable.
*                    Register values follow TI datasheet SLAS671 and its
*                    "play digital data through DAC" example script.
*
*******************************************************************************/
#ifndef TLV320DAC3100_H
#define TLV320DAC3100_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLV320_I2C_ADDR             (0x18U)

/* Return codes */
#define TLV320_OK                   (0)
#define TLV320_ERR_BUS              (-1)
#define TLV320_ERR_PARAM            (-2)
#define TLV320_ERR_CLOCK            (-3)
#define TLV320_ERR_STATE            (-4)

/* Output routing bits for tlv320_init() */
#define TLV320_OUT_SPEAKER          (1U)
#define TLV320_OUT_HEADPHONE        (2U)

/* Page 0 registers */
#define TLV320_P0_PAGE              (0U)
#define TLV320_P0_SW_RESET          (1U)
#define TLV320_P0_OT_FLAG           (3U)
#define TLV320_P0_CLK_MUX           (4U)
#define TLV320_P0_PLL_PR            (5U)
#define TLV320_P0_PLL_J             (6U)
#define TLV320_P0_PLL_D_MSB         (7U)
#define TLV320_P0_PLL_D_LSB         (8U)
#define TLV320_P0_NDAC              (11U)
#define TLV320_P0_MDAC              (12U)
#define TLV320_P0_DOSR_MSB          (13U)
#define TLV320_P0_DOSR_LSB          (14U)
#define TLV320_P0_IF_CTRL1          (27U)
#define TLV320_P0_DATA_OFFSET       (28U)
#define TLV320_P0_DAC_FLAG1         (37U)
#define TLV320_P0_IRQ_STICKY        (44U)
#define TLV320_P0_DAC_PRB           (60U)
#define TLV320_P0_DAC_DATAPATH      (63U)
#define TLV320_P0_DAC_VOL_CTRL      (64U)
#define TLV320_P0_DAC_LVOL          (65U)
#define TLV320_P0_DAC_RVOL          (66U)
#define TLV320_P0_BEEP_L            (71U)
#define TLV320_P0_BEEP_R            (72U)
#define TLV320_P0_BEEP_LEN_MSB      (73U)
#define TLV320_P0_BEEP_LEN_MID      (74U)
#define TLV320_P0_BEEP_LEN_LSB      (75U)
#define TLV320_P0_BEEP_SIN_MSB      (76U)
#define TLV320_P0_BEEP_SIN_LSB      (77U)
#define TLV320_P0_BEEP_COS_MSB      (78U)
#define TLV320_P0_BEEP_COS_LSB      (79U)
#define TLV320_P0_VOL_PIN_CTRL      (116U)

/* Page 1 registers */
#define TLV320_P1_HP_DRIVERS        (31U)
#define TLV320_P1_SPK_AMP           (32U)
#define TLV320_P1_HP_POP            (33U)
#define TLV320_P1_OUT_MIXER         (35U)
#define TLV320_P1_HPL_VOL           (36U)
#define TLV320_P1_HPR_VOL           (37U)
#define TLV320_P1_SPK_VOL           (38U)
#define TLV320_P1_HPL_DRV           (40U)
#define TLV320_P1_HPR_DRV           (41U)
#define TLV320_P1_SPK_DRV           (42U)

/* DAC digital volume range in 0.5 dB steps (register is signed 8-bit) */
#define TLV320_VOL_MIN_HALF_DB      (-127)  /* -63.5 dB */
#define TLV320_VOL_MAX_HALF_DB      (48)    /* +24 dB */

/* Bus access supplied by the platform. Each call is one complete I2C
 * transaction. Return 0 on success, negative on error. */
typedef struct
{
    int  (*write)(void* ctx, uint8_t addr7, const uint8_t* data, uint16_t len);
    int  (*read_reg)(void* ctx, uint8_t addr7, uint8_t reg, uint8_t* value);
    void (*delay_ms)(void* ctx, uint32_t ms);
    void* ctx;
} tlv320_bus_t;

/* DAC clock tree (see tlv320_clock_calc) */
typedef struct
{
    bool     use_pll;
    uint8_t  pll_p;         /* 1..8 */
    uint8_t  pll_r;         /* 1..16 */
    uint8_t  pll_j;         /* 4..63 */
    uint16_t pll_d;         /* 0..9999, always 0 here */
    uint8_t  ndac;          /* 1..128 */
    uint8_t  mdac;          /* 1..128 */
    uint16_t dosr;          /* 2..1024 */
    uint8_t  prb;           /* DAC processing block (1 or 25) */
    uint32_t codec_clkin_hz;
    uint32_t dac_clk_hz;
    uint32_t dac_mod_clk_hz;
    uint32_t fs_hz;
} tlv320_clock_t;

typedef struct
{
    const tlv320_bus_t* bus;
    tlv320_clock_t clock;
    uint8_t outputs;
    uint8_t spk_drv;        /* P1 R42 value when unmuted */
    int8_t  page;           /* cached page, -1 = unknown */
    bool    ready;
} tlv320_t;

typedef struct
{
    bool over_temperature;  /* P0 R3 bit1 = 0 */
    bool spk_short;         /* P1 R32 bit0 */
    bool hp_short;          /* P0 R44 bits 7:6 (sticky) */
    bool ldac_on, rdac_on, hpl_on, hpr_on, spk_on;  /* P0 R37 */
} tlv320_status_t;

/* Computes PLL / NDAC / MDAC / DOSR for an exact sample rate.
 * use_pll = true : PLL_CLKIN = MCLK, CODEC_CLKIN = PLL_CLK (80..110 MHz),
 *                  DAC_CLK <= 49.152 MHz, 2.8 MHz <= DAC_MOD_CLK <= 6.2 MHz,
 *                  DOSR multiple of 8, MDAC x DOSR / 32 >= 12, PRB_P25.
 * use_pll = false: CODEC_CLKIN = MCLK. Same rules when possible, otherwise
 *                  the Infineon library setting NDAC = MDAC = 1,
 *                  DOSR = MCLK / fs, PRB_P1 (no hardware beep).
 * Returns TLV320_OK or TLV320_ERR_CLOCK. */
int tlv320_clock_calc(uint32_t mclk_hz, uint32_t fs_hz, bool use_pll, tlv320_clock_t* out);

/* Software reset and full setup. The DAC comes up powered but muted.
 * spk_gain_db: 6, 12, 18 or 24. */
int tlv320_init(tlv320_t* dev, const tlv320_bus_t* bus, const tlv320_clock_t* clock,
                uint8_t outputs, int spk_gain_db);

/* DAC digital volume of both channels in 0.5 dB steps (-127..+48) */
int tlv320_set_volume_half_db(tlv320_t* dev, int half_db);

/* Soft-stepped digital mute of both DAC channels */
int tlv320_mute(tlv320_t* dev, bool mute);

/* Class-D speaker amplifier power (no-op if the speaker output is unused) */
int tlv320_speaker_power(tlv320_t* dev, bool on);

/* Hardware beep generator (PRB_P25 only). sin_q15 / cos_q15 =
 * 32767 * sin / cos(2 pi f / fs). atten_db 0..61 below full scale. */
int tlv320_beep(tlv320_t* dev, int16_t sin_q15, int16_t cos_q15,
                uint32_t samples, uint8_t atten_db);

int tlv320_read_status(tlv320_t* dev, tlv320_status_t* status);

/* Powers down outputs, DACs, dividers and PLL (lowest power, keeps I2C) */
int tlv320_power_down(tlv320_t* dev);

#ifdef __cplusplus
}
#endif

#endif /* TLV320DAC3100_H */

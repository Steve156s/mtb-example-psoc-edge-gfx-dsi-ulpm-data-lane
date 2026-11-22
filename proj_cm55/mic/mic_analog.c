/*******************************************************************************
* File Name        : mic_analog.c
*
* Description      : Stereo analog microphone capture. See mic_analog.h.
*
* Signal path (per mic, KIT_PSE84_AI / KIT_PSE84_EVAL):
*   IM73A135V01 -> external RC network -> CTB0 OAx (open loop opamp, the
*   feedback network is on the PCB) -> OAx output pin -> SAR mux (internal
*   CTB0_OAx_OUT) -> HS SAR, 16x accumulate -> FIFO buffer x
*
* Autonomous Controller state table (same as Infineon's mono example):
*   0: wait block ready, enable OA0 + OA1
*   1: wait TR_LPPASS_IN3 (16 kHz TCPWM pulse)
*   2: wait block ready, enable SAR
*   3: trigger SAR scan (both mux channels), wait until the FIFO write is done
*   4: go back to state 1
*
* Clocks / triggers that the official example sets in design.modus are set
* up here at run time so the repository's design.modus does not have to
* change (see mic_config.h for the resources used).
*
* NOTE: Not verified on hardware yet.
*
*******************************************************************************/
#include "mic_analog.h"
#include "mic_config.h"

#include <string.h>

#include "cy_pdl.h"
#include "cybsp.h"

#if (MIC_ENABLE_ANALOG)

/*******************************************************************************
* Macros
*******************************************************************************/
#define AMIC_FIFO_IDX           (0U)
#define AMIC_FIFO_BUF_MIC1      (0U)
#define AMIC_FIFO_BUF_MIC2      (1U)

/* Port 14 pins used by the two analog mic front ends */
#define AMIC_PORT               GPIO_PRT14
#define AMIC_PIN_FIRST          (0U)
#define AMIC_PIN_LAST           (7U)


/*******************************************************************************
* Autonomous Analog configuration (stereo version of amic_hq_mono_init)
*******************************************************************************/
static cy_en_autanalog_ac_out_trigger_mask_t amic_ac_out_trig_mask[] =
{
    CY_AUTANALOG_AC_OUT_TRIG_MASK_EMPTY, CY_AUTANALOG_AC_OUT_TRIG_MASK_EMPTY,
    CY_AUTANALOG_AC_OUT_TRIG_MASK_EMPTY, CY_AUTANALOG_AC_OUT_TRIG_MASK_EMPTY,
    CY_AUTANALOG_AC_OUT_TRIG_MASK_EMPTY, CY_AUTANALOG_AC_OUT_TRIG_MASK_EMPTY,
    CY_AUTANALOG_AC_OUT_TRIG_MASK_EMPTY, CY_AUTANALOG_AC_OUT_TRIG_MASK_EMPTY,
};

static cy_stc_autanalog_ac_t amic_ac_cfg =
{
    .gpioOutEn = CY_AUTANALOG_STT_AC_GPIO_OUT_DISABLED,
    .mask =
    {
        &amic_ac_out_trig_mask[0U], &amic_ac_out_trig_mask[1U],
        &amic_ac_out_trig_mask[2U], &amic_ac_out_trig_mask[3U],
        &amic_ac_out_trig_mask[4U], &amic_ac_out_trig_mask[5U],
        &amic_ac_out_trig_mask[6U], &amic_ac_out_trig_mask[7U],
    },
    .timer =
    {
        .enable = false,
        .clkSrc = CY_AUTANALOG_TIMER_CLK_LP,
        .period = 255U,
    },
};

#define AC_STATE(unlock_, cond_, action_, branch_)  \
    {                                               \
        .unlock = (unlock_),                        \
        .lpMode = false,                            \
        .condition = (cond_),                       \
        .action = (action_),                        \
        .branchState = (branch_),                   \
        .trigInt = false,                           \
        .count = 0U,                                \
        .unlockGpioOut = false,                     \
        .gpioOut = CY_AUTANALOG_STT_AC_GPIO_OUT_DISABLED, \
    }

static cy_stc_autanalog_stt_ac_t amic_ac_stt[] =
{
    AC_STATE(true,  CY_AUTANALOG_STT_AC_CONDITION_BLOCK_READY,    CY_AUTANALOG_STT_AC_ACTION_WAIT_FOR,       1U),
    AC_STATE(false, CY_AUTANALOG_STT_AC_CONDITION_TR_AUTANALOG_IN3, CY_AUTANALOG_STT_AC_ACTION_WAIT_FOR,     0U),
    AC_STATE(false, CY_AUTANALOG_STT_AC_CONDITION_BLOCK_READY,    CY_AUTANALOG_STT_AC_ACTION_WAIT_FOR,       0U),
    AC_STATE(false, CY_AUTANALOG_STT_AC_CONDITION_SAR_FIFO_DONE,  CY_AUTANALOG_STT_AC_ACTION_WAIT_FOR,       0U),
    AC_STATE(false, CY_AUTANALOG_STT_AC_CONDITION_TRUE,           CY_AUTANALOG_STT_AC_ACTION_BRANCH_IF_TRUE, 1U),
};

/* CTB0: both opamps as open-loop preamps (feedback network on the PCB) */
static cy_stc_autanalog_ctb_sta_t amic_ctb_sta_cfg =
{
    .pwrOpamp0      = MIC_AMIC_PREAMP_POWER,
    .pwrOpamp1      = MIC_AMIC_PREAMP_POWER,
    .topologyOpamp0 = CY_AUTANALOG_CTB_OA_TOPO_OPEN_LOOP_OPAMP,
    .topologyOpamp1 = CY_AUTANALOG_CTB_OA_TOPO_OPEN_LOOP_OPAMP,
    .intComp0       = CY_AUTANALOG_CTB_COMP_INT_EDGE_DISABLED,
    .intComp1       = CY_AUTANALOG_CTB_COMP_INT_EDGE_DISABLED,
    .capCcOpamp0    = CY_AUTANALOG_CTB_OA_CC_CAP_5_1_pF,
    .capCcOpamp1    = CY_AUTANALOG_CTB_OA_CC_CAP_5_1_pF,
    .capFbOpamp0    = CY_AUTANALOG_CTB_OA_FB_CAP_0_pF,
    .capFbOpamp1    = CY_AUTANALOG_CTB_OA_FB_CAP_0_pF,
};

/* Dynamic config 0 -> OA0 (AMIC1): + = P14.0 (INP), - = P14.1 (INN)
 * Dynamic config 1 -> OA1 (AMIC2): + = P14.4 (INP), - = P14.5 (INN)
 * The pin enums name the pad pair OA0/OA1 share: "OA0_P1_OA1_P4" selects
 * P1 for OA0 and P4 for OA1. */
static cy_stc_autanalog_ctb_dyn_t amic_ctb_dyn_cfg[] =
{
    {
        .ninvInpPin   = CY_AUTANALOG_CTB_OA_NINV_PIN_OA0_P0_OA1_P5,
        .ninvInpRef   = CY_AUTANALOG_CTB_OA_NINV_REF_DISCONNECT,
        .invInpPin    = CY_AUTANALOG_CTB_OA_INV_PIN_OA0_P1_OA1_P4,
        .resInpPin    = CY_AUTANALOG_CTB_OA_RES_PIN_DISCONNECT,
        .resInpRef    = CY_AUTANALOG_CTB_OA_RES_REF_DISCONNECT,
        .sharedMuxIn  = CY_AUTANALOG_CTB_OA_MUX_IN_DISCONNECT,
        .sharedMuxOut = CY_AUTANALOG_CTB_OA_MUX_OUT_DISCONNECT,
        .outToPin     = true,   /* P14.2 */
    },
    {
        .ninvInpPin   = CY_AUTANALOG_CTB_OA_NINV_PIN_OA0_P1_OA1_P4,
        .ninvInpRef   = CY_AUTANALOG_CTB_OA_NINV_REF_DISCONNECT,
        .invInpPin    = CY_AUTANALOG_CTB_OA_INV_PIN_OA0_P0_OA1_P5,
        .resInpPin    = CY_AUTANALOG_CTB_OA_RES_PIN_DISCONNECT,
        .resInpRef    = CY_AUTANALOG_CTB_OA_RES_REF_DISCONNECT,
        .sharedMuxIn  = CY_AUTANALOG_CTB_OA_MUX_IN_DISCONNECT,
        .sharedMuxOut = CY_AUTANALOG_CTB_OA_MUX_OUT_DISCONNECT,
        .outToPin     = true,   /* P14.3 */
    },
};

static cy_stc_autanalog_ctb_t amic_ctb_cfg =
{
    .ctbStaCfg    = &amic_ctb_sta_cfg,
    .ctbDynCfgNum = 2U,
    .ctbDynCfgArr = amic_ctb_dyn_cfg,
};

#define CTB_STATE(unlock_, en_)                                 \
    {                                                           \
        .unlock = (unlock_),                                    \
        .enableOpamp0 = (en_),                                  \
        .cfgOpamp0 = 0U,                                        \
        .gainOpamp0 = CY_AUTANALOG_STT_CTB_OA_GAIN_1_00,        \
        .enableOpamp1 = (en_),                                  \
        .cfgOpamp1 = 1U,                                        \
        .gainOpamp1 = CY_AUTANALOG_STT_CTB_OA_GAIN_1_00,        \
    }

static cy_stc_autanalog_stt_ctb_t amic_ctb_stt[] =
{
    CTB_STATE(true,  true),     /* state 0 turns both preamps on ...   */
    CTB_STATE(false, false),    /* ... later states leave them locked  */
    CTB_STATE(false, false),
    CTB_STATE(false, false),
    CTB_STATE(false, false),
};

/* SAR mux channel 0 = OA0 output -> FIFO buffer 0,
 * SAR mux channel 1 = OA1 output -> FIFO buffer 1 */
static cy_stc_autanalog_sar_mux_chan_t amic_sar_mux_cfg[] =
{
    {
        .posPin     = CY_AUTANALOG_SAR_PIN_MUX_CTB0_OA0_OUT,
        .sign       = true,
        .posCoeff   = CY_AUTANALOG_SAR_CH_COEFF_DISABLED,
        .negPin     = CY_AUTANALOG_SAR_PIN_MUX_VSSA,
        .buffBypass = true,
        .accShift   = true,
        .negCoeff   = CY_AUTANALOG_SAR_CH_COEFF_DISABLED,
        .muxLimit   = CY_AUTANALOG_SAR_LIMIT_STATUS_DISABLED,
        .fifoSel    = CY_AUTANALOG_FIFO_SELECT0,
    },
    {
        .posPin     = CY_AUTANALOG_SAR_PIN_MUX_CTB0_OA1_OUT,
        .sign       = true,
        .posCoeff   = CY_AUTANALOG_SAR_CH_COEFF_DISABLED,
        .negPin     = CY_AUTANALOG_SAR_PIN_MUX_VSSA,
        .buffBypass = true,
        .accShift   = true,
        .negCoeff   = CY_AUTANALOG_SAR_CH_COEFF_DISABLED,
        .muxLimit   = CY_AUTANALOG_SAR_LIMIT_STATUS_DISABLED,
        .fifoSel    = CY_AUTANALOG_FIFO_SELECT1,
    },
};

static cy_stc_autanalog_sar_sta_hs_t amic_sar_sta_hs_cfg =
{
    .hsVref       = CY_AUTANALOG_SAR_VREF_VDDA,
    .hsSampleTime = { 31U, 31U, 31U, 31U },
    .hsGpioChan   = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
    .hsGpioResultMask = CY_AUTANALOG_SAR_CHAN_MASK_GPIO_DISABLED,
};

static cy_stc_autanalog_sar_sta_t amic_sar_sta_cfg =
{
    .lpStaCfg   = NULL,
    .hsStaCfg   = &amic_sar_sta_hs_cfg,
    .posBufPwr  = CY_AUTANALOG_SAR_BUF_PWR_OFF,
    .negBufPwr  = CY_AUTANALOG_SAR_BUF_PWR_OFF,
    .accMode    = CY_AUTANALOG_SAR_ACC_ACCUNDUMP,
    .startupCal = CY_AUTANALOG_SAR_CAL_DISABLED,
    .chanID     = false,
    .shiftMode  = true,
    .intMuxChan =
    {
        &amic_sar_mux_cfg[0U], &amic_sar_mux_cfg[1U], NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    },
    .limitCond     = { NULL, NULL, NULL, NULL },
    .muxResultMask = CY_AUTANALOG_SAR_CHAN_MASK_MUX0 | CY_AUTANALOG_SAR_CHAN_MASK_MUX1,
    .firResultMask = CY_AUTANALOG_SAR_MASK_FIR_DISABLED,
};

/* One sequencer entry converts both mux channels back to back */
static cy_stc_autanalog_sar_seq_tab_hs_t amic_sar_seq_cfg[] =
{
    {
        .chanEn       = CY_AUTANALOG_SAR_CHAN_MASK_GPIO_DISABLED,
        .muxMode      = CY_AUTANALOG_SAR_CHAN_CFG_MUX0_MUX1_SINGLE_ENDED,
        .mux0Sel      = CY_AUTANALOG_SAR_CHAN_CFG_MUX0,
        .mux1Sel      = CY_AUTANALOG_SAR_CHAN_CFG_MUX1,
        .sampleTimeEn = true,
        .sampleTime   = CY_AUTANALOG_SAR_SAMPLE_TIME0,
        .accEn        = true,
        .accCount     = MIC_AMIC_ACCUMULATION,
        .calReq       = CY_AUTANALOG_SAR_CAL_DISABLED,
        .nextAction   = CY_AUTANALOG_SAR_NEXT_ACTION_STATE_STOP,
    },
};

/* FIFO split in two 256-word buffers, level interrupt at 160 words (10 ms) */
static cy_stc_autanalog_fifo_cfg_t amic_fifo_cfg =
{
    .split = CY_AUTANALOG_FIFO_SPLIT2,
    .level = { MIC_FRAME_SAMPLES - 1U, MIC_FRAME_SAMPLES - 1U, 0U, 0U, 0U, 0U, 0U, 0U },
};

static cy_stc_autanalog_sar_t amic_sar_cfg =
{
    .sarStaCfg   = &amic_sar_sta_cfg,
    .hsSeqTabNum = sizeof(amic_sar_seq_cfg) / sizeof(amic_sar_seq_cfg[0U]),
    .hsSeqTabArr = &amic_sar_seq_cfg[0U],
    .lpSeqTabNum = 0U,
    .lpSeqTabArr = NULL,
    .firNum      = 0U,
    .firCfg      = NULL,
    .fifoCfg     = &amic_fifo_cfg,
};

static cy_stc_autanalog_stt_sar_t amic_sar_stt[] =
{
    { .unlock = false, .enable = false, .trigger = false, .entryState = 0U },
    { .unlock = false, .enable = false, .trigger = false, .entryState = 0U },
    { .unlock = true,  .enable = true,  .trigger = false, .entryState = 0U },
    { .unlock = true,  .enable = true,  .trigger = true,  .entryState = 0U },
    { .unlock = true,  .enable = false, .trigger = false, .entryState = 0U },
};

static cy_stc_autanalog_cfg_t amic_cfg =
{
    .prb    = NULL,
    .ac     = &amic_ac_cfg,
    .ctb    = { &amic_ctb_cfg, NULL },
    .ptcomp = { NULL },
    .dac    = { NULL, NULL },
    .sar    = { &amic_sar_cfg },
};

#define STT_ENTRY(i)                                    \
    {                                                   \
        .ac     = &amic_ac_stt[(i)],                    \
        .prb    = NULL,                                 \
        .ctb    = { &amic_ctb_stt[(i)], NULL },         \
        .ptcomp = { NULL },                             \
        .dac    = { NULL, NULL },                       \
        .sar    = { &amic_sar_stt[(i)] },               \
    }

static cy_stc_autanalog_stt_t amic_stt[] =
{
    STT_ENTRY(0U), STT_ENTRY(1U), STT_ENTRY(2U), STT_ENTRY(3U), STT_ENTRY(4U),
};

static cy_stc_autanalog_t amic_init =
{
    .configuration        = &amic_cfg,
    .numSttEntries        = sizeof(amic_stt) / sizeof(amic_stt[0U]),
    .stateTransitionTable = &amic_stt[0U],
};


/*******************************************************************************
* Global Variables
*******************************************************************************/
static const cy_stc_sysint_t amic_fifo_irq_cfg =
{
    .intrSrc      = pass_interrupt_fifo_IRQn,
    .intrPriority = MIC_AMIC_IRQ_PRIORITY,
};

static int32_t  amic_raw[MIC_FRAME_SAMPLES];
static int16_t  amic_frames[2][2][MIC_FRAME_SAMPLES];
static volatile uint8_t  amic_fill_buf;
static volatile int8_t   amic_ready_buf = -1;
static volatile uint32_t amic_overrun_count;
static uint32_t          amic_rate_hz;
static mic_frame_cb_t    amic_cb;


/*******************************************************************************
* Function Name: amic_fifo_isr
********************************************************************************
* Summary:
*  Both FIFO buffers fill in lockstep (one scan writes MUX0 then MUX1), so the
*  interrupt is taken on buffer 1 and both buffers are emptied here.
*******************************************************************************/
static void amic_fifo_isr(void)
{
    uint32_t status = Cy_AutAnalog_FIFO_GetInterruptStatus(AMIC_FIFO_IDX);

    Cy_AutAnalog_FIFO_ClearInterruptMask(AMIC_FIFO_IDX, CY_AUTANALOG_INT_FIFO_LEVEL1);
    Cy_AutAnalog_FIFO_ClearInterrupt(AMIC_FIFO_IDX, status);

    if (0U != (status & (uint32_t) CY_AUTANALOG_INT_FIFO_LEVEL1))
    {
        uint8_t b = amic_fill_buf;

        (void) Cy_AutAnalog_FIFO_ReadData(AMIC_FIFO_IDX, AMIC_FIFO_BUF_MIC1,
                                          MIC_FRAME_SAMPLES, amic_raw);
        for (uint32_t i = 0U; i < MIC_FRAME_SAMPLES; i++)
        {
            /* 12-bit x16 accumulate, signed -> 16-bit value */
            amic_frames[b][0][i] = (int16_t) amic_raw[i];
        }

        (void) Cy_AutAnalog_FIFO_ReadData(AMIC_FIFO_IDX, AMIC_FIFO_BUF_MIC2,
                                          MIC_FRAME_SAMPLES, amic_raw);
        for (uint32_t i = 0U; i < MIC_FRAME_SAMPLES; i++)
        {
            amic_frames[b][1][i] = (int16_t) amic_raw[i];
        }

        if (amic_ready_buf >= 0)
        {
            amic_overrun_count++;
        }
        amic_ready_buf = (int8_t) b;
        amic_fill_buf = b ^ 1U;

        if (NULL != amic_cb)
        {
            amic_cb();
        }
    }

    Cy_AutAnalog_FIFO_SetInterruptMask(AMIC_FIFO_IDX, CY_AUTANALOG_INT_FIFO_LEVEL1);
}


/*******************************************************************************
* Function Name: amic_pins_init
********************************************************************************
* Summary:
*  P14.0..P14.7 to analog mode (CTB pads, mic bias references).
*******************************************************************************/
static void amic_pins_init(void)
{
    for (uint32_t pin = AMIC_PIN_FIRST; pin <= AMIC_PIN_LAST; pin++)
    {
        Cy_GPIO_Pin_FastInit(AMIC_PORT, pin, CY_GPIO_DM_ANALOG, 0U, HSIOM_SEL_GPIO);
    }
}


/*******************************************************************************
* Function Name: amic_clocks_init
********************************************************************************
* Summary:
*  LPPASS charge pump clock and the 16 kHz sample timer (TCPWM0 counter 256,
*  trigger out 0 -> pass.tr_lppass_in[3]).
*******************************************************************************/
static cy_rslt_t amic_clocks_init(void)
{
    cy_stc_tcpwm_counter_config_t cnt_cfg;
    uint32_t clk_hz;

    /* Charge pump clock for the CTB rail-to-rail inputs */
    (void) Cy_SysClk_PeriPclkDisableDivider(MIC_AMIC_PUMP_PCLK, CY_SYSCLK_DIV_8_BIT,
                                            MIC_AMIC_PUMP_DIV_NUM);
    if ((CY_SYSCLK_SUCCESS != Cy_SysClk_PeriPclkSetDivider(MIC_AMIC_PUMP_PCLK, CY_SYSCLK_DIV_8_BIT,
                                                          MIC_AMIC_PUMP_DIV_NUM,
                                                          MIC_AMIC_PUMP_DIV_VALUE - 1U)) ||
        (CY_SYSCLK_SUCCESS != Cy_SysClk_PeriPclkEnableDivider(MIC_AMIC_PUMP_PCLK, CY_SYSCLK_DIV_8_BIT,
                                                             MIC_AMIC_PUMP_DIV_NUM)) ||
        (CY_SYSCLK_SUCCESS != Cy_SysClk_PeriPclkAssignDivider(MIC_AMIC_PUMP_PCLK, CY_SYSCLK_DIV_8_BIT,
                                                             MIC_AMIC_PUMP_DIV_NUM)))
    {
        return MIC_RSLT_ERROR;
    }

    /* Timer clock: undivided peripheral clock of group 1 */
    (void) Cy_SysClk_PeriPclkDisableDivider(MIC_AMIC_TCPWM_PCLK, CY_SYSCLK_DIV_8_BIT,
                                            MIC_AMIC_TCPWM_DIV_NUM);
    if ((CY_SYSCLK_SUCCESS != Cy_SysClk_PeriPclkSetDivider(MIC_AMIC_TCPWM_PCLK, CY_SYSCLK_DIV_8_BIT,
                                                          MIC_AMIC_TCPWM_DIV_NUM, 0U)) ||
        (CY_SYSCLK_SUCCESS != Cy_SysClk_PeriPclkEnableDivider(MIC_AMIC_TCPWM_PCLK, CY_SYSCLK_DIV_8_BIT,
                                                             MIC_AMIC_TCPWM_DIV_NUM)) ||
        (CY_SYSCLK_SUCCESS != Cy_SysClk_PeriPclkAssignDivider(MIC_AMIC_TCPWM_PCLK, CY_SYSCLK_DIV_8_BIT,
                                                             MIC_AMIC_TCPWM_DIV_NUM)))
    {
        return MIC_RSLT_ERROR;
    }

    clk_hz = Cy_SysClk_PeriPclkGetFrequency(MIC_AMIC_TCPWM_PCLK, CY_SYSCLK_DIV_8_BIT,
                                            MIC_AMIC_TCPWM_DIV_NUM);
    if ((clk_hz < MIC_SAMPLE_RATE_HZ) || ((clk_hz / MIC_SAMPLE_RATE_HZ) > 65536U))
    {
        return MIC_RSLT_ERROR;   /* group 1 counters are 16 bit */
    }
    amic_rate_hz = clk_hz / (clk_hz / MIC_SAMPLE_RATE_HZ);

    (void) memset(&cnt_cfg, 0, sizeof(cnt_cfg));
    cnt_cfg.period           = (clk_hz / MIC_SAMPLE_RATE_HZ) - 1U;
    cnt_cfg.clockPrescaler   = CY_TCPWM_COUNTER_PRESCALER_DIVBY_1;
    cnt_cfg.runMode          = CY_TCPWM_COUNTER_CONTINUOUS;
    cnt_cfg.countDirection   = CY_TCPWM_COUNTER_COUNT_UP;
    cnt_cfg.compareOrCapture = CY_TCPWM_COUNTER_MODE_CAPTURE;
    cnt_cfg.interruptSources = CY_TCPWM_INT_NONE;
    cnt_cfg.captureInputMode = CY_TCPWM_INPUT_LEVEL;
    cnt_cfg.captureInput     = CY_TCPWM_INPUT_0;
    cnt_cfg.reloadInputMode  = CY_TCPWM_INPUT_LEVEL;
    cnt_cfg.reloadInput      = CY_TCPWM_INPUT_0;
    cnt_cfg.startInputMode   = CY_TCPWM_INPUT_LEVEL;
    cnt_cfg.startInput       = CY_TCPWM_INPUT_0;
    cnt_cfg.stopInputMode    = CY_TCPWM_INPUT_LEVEL;
    cnt_cfg.stopInput        = CY_TCPWM_INPUT_0;
    cnt_cfg.countInputMode   = CY_TCPWM_INPUT_LEVEL;
    cnt_cfg.countInput       = CY_TCPWM_INPUT_1;
#if (CY_IP_MXTCPWM_VERSION >= 2U)
    cnt_cfg.capture1InputMode = CY_TCPWM_INPUT_LEVEL;
    cnt_cfg.capture1Input     = CY_TCPWM_INPUT_0;
    cnt_cfg.trigger0Event     = CY_TCPWM_CNT_TRIGGER_ON_TC;   /* pulse each period */
    cnt_cfg.trigger1Event     = CY_TCPWM_CNT_TRIGGER_ON_DISABLED;
#endif

    if (CY_TCPWM_SUCCESS != Cy_TCPWM_Counter_Init(MIC_AMIC_TCPWM_BASE, MIC_AMIC_TCPWM_CNT, &cnt_cfg))
    {
        return MIC_RSLT_ERROR;
    }

    /* TCPWM tr_out0 -> LPPASS trigger input 3 (state 1 waits for it) */
    if (CY_TRIGMUX_SUCCESS != Cy_TrigMux_Connect(MIC_AMIC_TCPWM_TRIG_IN, MIC_AMIC_TRIG_OUT,
                                                 false, TRIGGER_TYPE_EDGE))
    {
        return MIC_RSLT_ERROR;
    }

    return CY_RSLT_SUCCESS;
}


/*******************************************************************************
* Function Name: mic_analog_init
*******************************************************************************/
cy_rslt_t mic_analog_init(mic_frame_cb_t frame_ready_cb)
{
    cy_rslt_t result;

    amic_cb = frame_ready_cb;

    amic_pins_init();

    result = amic_clocks_init();
    if (CY_RSLT_SUCCESS != result)
    {
        return result;
    }

    if (CY_AUTANALOG_SUCCESS != Cy_AutAnalog_Init(&amic_init))
    {
        return MIC_RSLT_ERROR;
    }

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&amic_fifo_irq_cfg, &amic_fifo_isr))
    {
        return MIC_RSLT_ERROR;
    }
    NVIC_ClearPendingIRQ(amic_fifo_irq_cfg.intrSrc);
    NVIC_EnableIRQ(amic_fifo_irq_cfg.intrSrc);
    Cy_AutAnalog_FIFO_SetInterruptMask(AMIC_FIFO_IDX, CY_AUTANALOG_INT_FIFO_LEVEL1);

    /* Start the state machine, then the 16 kHz timer that paces it */
    Cy_AutAnalog_StartAutonomousControl();
    Cy_TCPWM_Counter_Enable(MIC_AMIC_TCPWM_BASE, MIC_AMIC_TCPWM_CNT);
    Cy_TCPWM_TriggerStart_Single(MIC_AMIC_TCPWM_BASE, MIC_AMIC_TCPWM_CNT);

    return CY_RSLT_SUCCESS;
}


/*******************************************************************************
* Function Name: mic_analog_stop
*******************************************************************************/
void mic_analog_stop(void)
{
    Cy_TCPWM_Counter_Disable(MIC_AMIC_TCPWM_BASE, MIC_AMIC_TCPWM_CNT);
    NVIC_DisableIRQ(amic_fifo_irq_cfg.intrSrc);
    Cy_AutAnalog_Disable();
}


/*******************************************************************************
* Function Name: mic_analog_read_frame
*******************************************************************************/
bool mic_analog_read_frame(int16_t *mic1, int16_t *mic2)
{
    int8_t b;
    uint32_t irq;

    irq = Cy_SysLib_EnterCriticalSection();
    b = amic_ready_buf;
    amic_ready_buf = -1;
    Cy_SysLib_ExitCriticalSection(irq);

    if (b < 0)
    {
        return false;
    }

    (void) memcpy(mic1, amic_frames[b][0], sizeof(amic_frames[0][0]));
    (void) memcpy(mic2, amic_frames[b][1], sizeof(amic_frames[0][1]));
    return true;
}


uint32_t mic_analog_overruns(void)    { return amic_overrun_count; }
uint32_t mic_analog_sample_rate(void) { return amic_rate_hz; }

#else /* MIC_ENABLE_ANALOG == 0 */

cy_rslt_t mic_analog_init(mic_frame_cb_t frame_ready_cb) { (void) frame_ready_cb; return MIC_RSLT_ERROR; }
void mic_analog_stop(void) { }
bool mic_analog_read_frame(int16_t *mic1, int16_t *mic2) { (void) mic1; (void) mic2; return false; }
uint32_t mic_analog_overruns(void) { return 0U; }
uint32_t mic_analog_sample_rate(void) { return 0U; }

#endif /* MIC_ENABLE_ANALOG */

/* [] END OF FILE */

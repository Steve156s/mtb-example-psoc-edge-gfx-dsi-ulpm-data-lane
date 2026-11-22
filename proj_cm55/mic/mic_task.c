/*******************************************************************************
* File Name        : mic_task.c
*
* Description      : 4-microphone task. See mic_task.h.
*
* NOTE: Not verified on hardware yet.
*
*******************************************************************************/
#include "mic_task.h"
#include "mic_config.h"
#include "mic_level.h"
#include "mic_pdm.h"
#include "mic_analog.h"

#include <stdio.h>
#include <string.h>

#include "cy_pdl.h"
#include "FreeRTOS.h"
#include "task.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define MIC_TASK_NAME           ("Mic Task")
#define MIC_TASK_STACK_WORDS    (configMINIMAL_STACK_SIZE * 8)

#define NOTIFY_PDM              (1UL << 0)
#define NOTIFY_AMIC             (1UL << 1)
#define NOTIFY_TOGGLE           (1UL << 2)

/* A channel shows "CLIP" for this many frames after the last clipped sample */
#define CLIP_SHOW_FRAMES        (100U)

/* Characters of each UART VU bar */
#define UART_BAR_CHARS          (10U)


/*******************************************************************************
* Global Variables
*******************************************************************************/
static TaskHandle_t mic_task_handle;

static const char *const ch_label[MIC_NUM_CHANNELS] = { "D-L", "D-R", "A-1", "A-2" };

static mic_level_t ch_level[MIC_NUM_CHANNELS];
static uint32_t    ch_last_clips[MIC_NUM_CHANNELS];
static uint16_t    ch_clip_show[MIC_NUM_CHANNELS];
static bool        ch_ok[MIC_NUM_CHANNELS];
static mic_vad_t   amic_vad;

static int16_t frame_buf[MIC_NUM_CHANNELS][MIC_FRAME_SAMPLES];

static volatile bool wake_mode = (MIC_WAKE_MODE_DEFAULT != 0);
static uint32_t      wake_hold;

/* HUD snapshot, written by the mic task, read by the GFX task */
static mic_hud_view_t hud_view;


/*******************************************************************************
* Function Name: mic_task_frame_hook (weak default: does nothing)
*******************************************************************************/
__WEAK void mic_task_frame_hook(const int16_t *const ch[4], const bool valid[4])
{
    (void) ch;
    (void) valid;
}


/*******************************************************************************
* Interrupt callbacks from the drivers
*******************************************************************************/
static void notify_from_isr(uint32_t bits)
{
    BaseType_t woken = pdFALSE;

    if (NULL != mic_task_handle)
    {
        (void) xTaskNotifyFromISR(mic_task_handle, bits, eSetBits, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

static void pdm_frame_cb(void)  { notify_from_isr(NOTIFY_PDM); }
static void amic_frame_cb(void) { notify_from_isr(NOTIFY_AMIC); }


/*******************************************************************************
* Function Name: mic_task_request_mode_toggle
*******************************************************************************/
void mic_task_request_mode_toggle(void)
{
    if (0U != __get_IPSR())
    {
        notify_from_isr(NOTIFY_TOGGLE);
    }
    else if (NULL != mic_task_handle)
    {
        (void) xTaskNotify(mic_task_handle, NOTIFY_TOGGLE, eSetBits);
    }
}


/*******************************************************************************
* Function Name: update_channel
*******************************************************************************/
static void update_channel(uint32_t ch)
{
    mic_level_t *lv = &ch_level[ch];

    mic_level_process(lv, frame_buf[ch], MIC_FRAME_SAMPLES);

    if (lv->clip_count != ch_last_clips[ch])
    {
        ch_last_clips[ch] = lv->clip_count;
        ch_clip_show[ch] = CLIP_SHOW_FRAMES;
    }
    else if (ch_clip_show[ch] > 0U)
    {
        ch_clip_show[ch]--;
    }
}


/*******************************************************************************
* Function Name: publish_view
*******************************************************************************/
static void publish_view(void)
{
    mic_hud_view_t v;
    bool pdm_on = mic_pdm_is_running();

    for (uint32_t i = 0U; i < MIC_NUM_CHANNELS; i++)
    {
        const mic_level_t *lv = &ch_level[i];
        bool is_pdm = (i <= MIC_CH_PDM_R);

        v.ch[i].label = ch_label[i];
        if (!ch_ok[i])
        {
            v.ch[i].status = MIC_HUD_CH_ERROR;
        }
        else if (is_pdm && !pdm_on)
        {
            v.ch[i].status = MIC_HUD_CH_OFF;
        }
        else
        {
            v.ch[i].status = MIC_HUD_CH_OK;
        }
        v.ch[i].vu_fill   = mic_level_vu_fill(lv, lv->vu_db10, MIC_HUD_BAR_W);
        v.ch[i].hold_fill = mic_level_vu_fill(lv, lv->hold_db10, MIC_HUD_BAR_W);
        v.ch[i].rms_db10  = lv->rms_db10;
        v.ch[i].clipping  = (ch_clip_show[i] > 0U);
    }
    v.wake_mode  = wake_mode;
    v.vad_active = amic_vad.active;
    v.floor_db10 = mic_vad_floor_db10(&amic_vad);

    taskENTER_CRITICAL();
    hud_view = v;
    taskEXIT_CRITICAL();
}


/*******************************************************************************
* Function Name: mic_task_get_view
*******************************************************************************/
void mic_task_get_view(mic_hud_view_t *view)
{
    taskENTER_CRITICAL();
    *view = hud_view;
    taskEXIT_CRITICAL();
}


/*******************************************************************************
* Function Name: print_vu
********************************************************************************
* Summary:
*  One UART line, e.g.
*  D-L -32 [#####     ] D-R -33 [#####     ] A-1 -41 [###       ] ... VAD- FLR -55
*******************************************************************************/
static void print_vu(void)
{
    char line[160];
    uint32_t n = 0U;

    for (uint32_t i = 0U; i < MIC_NUM_CHANNELS; i++)
    {
        const mic_level_t *lv = &ch_level[i];
        uint32_t fill = mic_level_vu_fill(lv, lv->vu_db10, UART_BAR_CHARS);
        int db = (lv->rms_db10 - 5) / 10;

        if (!ch_ok[i] || ((i <= MIC_CH_PDM_R) && !mic_pdm_is_running()))
        {
            n += (uint32_t) snprintf(&line[n], sizeof(line) - n, "%s --- [%s] ",
                                     ch_label[i], ch_ok[i] ? "   off    " : "  error   ");
            continue;
        }

        n += (uint32_t) snprintf(&line[n], sizeof(line) - n, "%s %3d [", ch_label[i], db);
        for (uint32_t c = 0U; c < UART_BAR_CHARS; c++)
        {
            line[n++] = (c < fill) ? '#' : ' ';
        }
        n += (uint32_t) snprintf(&line[n], sizeof(line) - n, "]%c ",
                                 (ch_clip_show[i] > 0U) ? '!' : ' ');
    }

    (void) snprintf(&line[n], sizeof(line) - n, "VAD%c FLR %d%s",
                    amic_vad.active ? '+' : '-',
                    (mic_vad_floor_db10(&amic_vad) - 5) / 10,
                    wake_mode ? " WAKE" : "");
    printf("%s\r\n", line);
}


/*******************************************************************************
* Function Name: handle_vad
*******************************************************************************/
static void handle_vad(void)
{
    /* louder of the two analog mics drives the detector */
    int16_t lvl = ch_level[MIC_CH_AMIC_1].rms_db10;
    mic_vad_event_t ev;

    if (ch_level[MIC_CH_AMIC_2].rms_db10 > lvl)
    {
        lvl = ch_level[MIC_CH_AMIC_2].rms_db10;
    }

    ev = mic_vad_process(&amic_vad, lvl);

    if (MIC_VAD_EVENT_START == ev)
    {
        printf("[MIC] sound activity detected (%d dBFS, floor %d)\r\n",
               (lvl - 5) / 10, (mic_vad_floor_db10(&amic_vad) - 5) / 10);
        if (wake_mode)
        {
            if (!mic_pdm_is_running() && ch_ok[MIC_CH_PDM_L])
            {
                printf("[MIC] wake: PDM mics on\r\n");
                mic_pdm_start();
            }
            wake_hold = MIC_WAKE_HOLD_FRAMES;
        }
    }
    else if (MIC_VAD_EVENT_END == ev)
    {
        printf("[MIC] sound activity ended\r\n");
    }

    if (wake_mode && mic_pdm_is_running() && !amic_vad.active)
    {
        if (wake_hold > 0U)
        {
            wake_hold--;
        }
        else
        {
            printf("[MIC] wake: quiet, PDM mics off\r\n");
            mic_pdm_stop();
        }
    }
}


/*******************************************************************************
* Function Name: mic_task
*******************************************************************************/
static void mic_task(void *arg)
{
    uint32_t bits = 0U;
    uint32_t frames = 0U;
    bool valid[MIC_NUM_CHANNELS];
    const int16_t *chp[MIC_NUM_CHANNELS] =
    {
        frame_buf[0], frame_buf[1], frame_buf[2], frame_buf[3]
    };

    (void) arg;

    for (uint32_t i = 0U; i < MIC_NUM_CHANNELS; i++)
    {
        mic_level_init(&ch_level[i], (int16_t)(MIC_VU_MIN_DBFS * 10),
                       MIC_VU_RELEASE_DB10_PER_FRAME, MIC_VU_PEAK_HOLD_FRAMES,
                       MIC_CLIP_LEVEL);
    }
    mic_vad_init(&amic_vad, MIC_VAD_ON_DB10, MIC_VAD_OFF_DB10, MIC_VAD_ABS_MIN_DB10,
                 MIC_VAD_ON_FRAMES, MIC_VAD_HANGOVER_FRAMES);

    /* Digital mics */
    ch_ok[MIC_CH_PDM_L] = ch_ok[MIC_CH_PDM_R] = (CY_RSLT_SUCCESS == mic_pdm_init(pdm_frame_cb));
    printf("[MIC] PDM 2ch init %s (24-bit, SW gain %d dB)\r\n",
           ch_ok[MIC_CH_PDM_L] ? "OK" : "FAILED", MIC_PDM_SW_GAIN_DB);

    /* Analog mics */
    ch_ok[MIC_CH_AMIC_1] = ch_ok[MIC_CH_AMIC_2] = (CY_RSLT_SUCCESS == mic_analog_init(amic_frame_cb));
    if (ch_ok[MIC_CH_AMIC_1])
    {
        printf("[MIC] analog 2ch init OK (%lu Hz)\r\n", (unsigned long) mic_analog_sample_rate());
    }
    else
    {
        printf("[MIC] analog 2ch init FAILED\r\n");
    }

    if (!ch_ok[MIC_CH_AMIC_1] && wake_mode)
    {
        /* Wake mode needs the analog VAD */
        wake_mode = false;
    }
    if (!wake_mode && ch_ok[MIC_CH_PDM_L])
    {
        mic_pdm_start();
    }

    printf("[MIC] mode: %s (USER BTN2 toggles)\r\n", wake_mode ? "wake" : "always on");
    publish_view();

    for (;;)
    {
        (void) xTaskNotifyWait(0U, 0xFFFFFFFFUL, &bits, pdMS_TO_TICKS(100U));

        if (0U != (bits & NOTIFY_TOGGLE))
        {
            wake_mode = (!wake_mode) && ch_ok[MIC_CH_AMIC_1];
            if (wake_mode)
            {
                wake_hold = 0U;   /* PDM goes off at the next quiet frame */
            }
            else if (ch_ok[MIC_CH_PDM_L])
            {
                mic_pdm_start();
            }
            printf("[MIC] mode: %s\r\n", wake_mode ? "wake (PDM off until sound)" : "always on");
        }

        valid[MIC_CH_PDM_L] = valid[MIC_CH_PDM_R] = false;
        valid[MIC_CH_AMIC_1] = valid[MIC_CH_AMIC_2] = false;

        if ((0U != (bits & NOTIFY_PDM)) &&
            mic_pdm_read_frame(frame_buf[MIC_CH_PDM_L], frame_buf[MIC_CH_PDM_R]))
        {
            update_channel(MIC_CH_PDM_L);
            update_channel(MIC_CH_PDM_R);
            valid[MIC_CH_PDM_L] = valid[MIC_CH_PDM_R] = true;
        }

        if ((0U != (bits & NOTIFY_AMIC)) &&
            mic_analog_read_frame(frame_buf[MIC_CH_AMIC_1], frame_buf[MIC_CH_AMIC_2]))
        {
            update_channel(MIC_CH_AMIC_1);
            update_channel(MIC_CH_AMIC_2);
            valid[MIC_CH_AMIC_1] = valid[MIC_CH_AMIC_2] = true;
            handle_vad();
        }

        if (valid[MIC_CH_AMIC_1] || (!ch_ok[MIC_CH_AMIC_1] && valid[MIC_CH_PDM_L]))
        {
            /* one "tick" per 10 ms: analog clock if present, else PDM */
            mic_task_frame_hook(chp, valid);
            publish_view();
            frames++;

#if (MIC_UART_PERIOD_FRAMES > 0)
            if (0U == (frames % MIC_UART_PERIOD_FRAMES))
            {
                print_vu();
            }
#endif
        }
    }
}


/*******************************************************************************
* Function Name: mic_task_start
*******************************************************************************/
cy_rslt_t mic_task_start(uint32_t priority)
{
    if (pdPASS != xTaskCreate(mic_task, MIC_TASK_NAME, MIC_TASK_STACK_WORDS,
                              NULL, (UBaseType_t) priority, &mic_task_handle))
    {
        return MIC_RSLT_ERROR;
    }
    return CY_RSLT_SUCCESS;
}

/* [] END OF FILE */

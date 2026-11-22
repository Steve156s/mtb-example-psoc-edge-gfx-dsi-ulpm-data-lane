/*******************************************************************************
* File Name        : audio_player.c
*
* Description      : Audio task: owns the TLV320DAC3100 and the I2S stream.
*
*   Power states
*     asleep : I2S stopped (no MCLK), DAC muted, Class-D amplifier off
*     awake  : I2S running (silence when idle), amplifier on
*     playing: source rendered into the I2S ping-pong blocks
*   A request wakes the chain (start I2S, wait for the codec PLL, amplifier
*   on, unmute). When the queue is empty the tail is played out, the DAC is
*   muted, and after AUDIO_IDLE_POWERDOWN_MS without a request the chain
*   goes back to sleep. The headphone drivers stay powered (their de-pop
*   power-on takes 1.2 s).
*
*******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "audio_player.h"
#include "audio_config.h"
#include "audio_i2c_psoc.h"
#include "audio_i2s_psoc.h"
#include "tlv320dac3100.h"

#define CMD_PLAY            (0U)
#define CMD_STOP            (1U)
#define PLL_RELOCK_MS       (10U)
#define DRAIN_POLL_MS       (5U)

typedef struct
{
    uint8_t cmd;
    uint8_t prio;
    audio_seq_t seq;
} audio_msg_t;

static StaticTask_t task_tcb;
static StackType_t task_stack[AUDIO_TASK_STACK_WORDS];
static TaskHandle_t task_handle = NULL;

static StaticQueue_t queue_tcb;
static uint8_t queue_storage[AUDIO_QUEUE_LENGTH * sizeof(audio_msg_t)];
static QueueHandle_t queue = NULL;

static CySCB_Type* i2c_base;
static cy_stc_scb_i2c_context_t* i2c_ctx;

static tlv320_t dac;
static const tlv320_bus_t* bus;
static audio_source_t source;

static volatile uint8_t volume = AUDIO_VOLUME_DEFAULT;
static volatile bool volume_dirty = true;
static volatile audio_player_status_t status;


static uint8_t dac_outputs(void)
{
    uint8_t o = 0U;
    if (0U != (AUDIO_OUTPUT & AUDIO_OUTPUT_SPEAKER))   { o |= TLV320_OUT_SPEAKER; }
    if (0U != (AUDIO_OUTPUT & AUDIO_OUTPUT_HEADPHONE)) { o |= TLV320_OUT_HEADPHONE; }
    return o;
}


static int volume_half_db(uint8_t level)
{
    return -((int)(AUDIO_VOLUME_MAX - level) * AUDIO_VOLUME_STEP_DB * 2);
}


static bool dac_setup(void)
{
    tlv320_clock_t clk;
    int rc = tlv320_clock_calc(AUDIO_MCLK_HZ, AUDIO_SAMPLE_RATE_HZ, (0 != AUDIO_DAC_USE_PLL), &clk);

    if (TLV320_OK != rc)
    {
        printf("AUDIO: no DAC clock setting for MCLK %lu Hz / fs %lu Hz\r\n",
               (unsigned long)AUDIO_MCLK_HZ, (unsigned long)AUDIO_SAMPLE_RATE_HZ);
        return false;
    }

    rc = tlv320_init(&dac, bus, &clk, dac_outputs(), AUDIO_SPK_GAIN_DB);
    status.clock = clk;

    if (TLV320_OK != rc)
    {
        printf("AUDIO: TLV320DAC3100 not answering at 0x%02X (rc %d)\r\n", TLV320_I2C_ADDR, rc);
        return false;
    }

    if (clk.use_pll)
    {
        printf("AUDIO: TLV320DAC3100 ready, PLL P=%u R=%u J=%u -> %lu Hz, NDAC=%u MDAC=%u DOSR=%u, PRB_P%u, fs=%lu Hz\r\n",
               clk.pll_p, clk.pll_r, clk.pll_j, (unsigned long)clk.codec_clkin_hz,
               clk.ndac, clk.mdac, clk.dosr, clk.prb, (unsigned long)clk.fs_hz);
    }
    else
    {
        printf("AUDIO: TLV320DAC3100 ready, MCLK direct, NDAC=%u MDAC=%u DOSR=%u, PRB_P%u, fs=%lu Hz\r\n",
               clk.ndac, clk.mdac, clk.dosr, clk.prb, (unsigned long)clk.fs_hz);
    }

    volume_dirty = true;
    return true;
}


static void apply_volume(void)
{
    if (volume_dirty && status.dac_ok)
    {
        volume_dirty = false;
        (void)tlv320_set_volume_half_db(&dac, volume_half_db(volume));

        /* 0 = mute; otherwise unmute only while something is playing */
        if (0U == volume)
        {
            (void)tlv320_mute(&dac, true);
        }
        else if (status.playing)
        {
            (void)tlv320_mute(&dac, false);
        }
    }
}


static void wake(void)
{
    if (status.awake)
    {
        return;
    }

    audio_i2s_start();                          /* MCLK back on */
    vTaskDelay(pdMS_TO_TICKS(PLL_RELOCK_MS));
    (void)tlv320_speaker_power(&dac, true);
    status.awake = true;
}


static void sleep_chain(void)
{
    if (!status.awake)
    {
        return;
    }

    (void)tlv320_mute(&dac, true);
    (void)tlv320_speaker_power(&dac, false);
    audio_i2s_stop();
    status.awake = false;
}


static void begin(const audio_msg_t* m)
{
    if (!status.dac_ok)
    {
        audio_i2s_start();                      /* codec PLL needs MCLK */
        status.dac_ok = dac_setup();
        status.awake = status.dac_ok;
        if (!status.dac_ok)
        {
            audio_i2s_stop();
            return;
        }
    }

    wake();
    apply_volume();
    if (volume > 0U)
    {
        (void)tlv320_mute(&dac, false);
    }

    audio_source_start(&source, &m->seq, AUDIO_SAMPLE_RATE_HZ, AUDIO_WORD_GAP_MS);
    status.playing = true;
}


static void finish(void)
{
    status.playing = false;
    status.played++;
}


static void audio_task(void* arg)
{
    audio_msg_t m;
    bool draining = false;
    TickType_t idle_since = xTaskGetTickCount();

    (void)arg;

    bus = audio_i2c_psoc_bind(i2c_base, i2c_ctx);

    if ((NULL == bus) || (CY_RSLT_SUCCESS != audio_i2s_init(task_handle)))
    {
        printf("AUDIO: init failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    /* MCLK must run while the codec PLL starts */
    audio_i2s_start();
    status.dac_ok = dac_setup();
    status.awake = true;
    apply_volume();

    for (;;)
    {
        int16_t* blk;
        size_t n;

        apply_volume();

        if (!status.playing)
        {
            TickType_t wait = portMAX_DELAY;

            if (draining)
            {
                wait = pdMS_TO_TICKS(DRAIN_POLL_MS);
            }
            else if (status.awake)
            {
                TickType_t elapsed = xTaskGetTickCount() - idle_since;
                TickType_t limit = pdMS_TO_TICKS(AUDIO_IDLE_POWERDOWN_MS);
                wait = (elapsed >= limit) ? 0U : (limit - elapsed);
            }

            if (pdTRUE == xQueueReceive(queue, &m, wait))
            {
                if (CMD_PLAY == m.cmd)
                {
                    draining = false;
                    begin(&m);
                }
                continue;
            }

            if (draining)
            {
                if (audio_i2s_idle())
                {
                    /* Tail played out: mute, start the idle timer */
                    (void)tlv320_mute(&dac, true);
                    draining = false;
                    idle_since = xTaskGetTickCount();
                }
            }
            else if (status.awake)
            {
                sleep_chain();
            }
            continue;
        }

        /* Urgent requests and STOP are sent to the queue front */
        if (pdTRUE == xQueuePeek(queue, &m, 0U))
        {
            if ((CMD_STOP == m.cmd) || (AUDIO_PRIO_URGENT == m.prio))
            {
                (void)xQueueReceive(queue, &m, 0U);
                finish();

                if (CMD_STOP == m.cmd)
                {
                    (void)xQueueReset(queue);
                    draining = true;
                    continue;
                }
                begin(&m);
            }
        }

        blk = audio_i2s_get_free_block();
        if (NULL == blk)
        {
            /* Both blocks queued: wait until the ISR has consumed one */
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50U));
            continue;
        }

        n = audio_source_read(&source, blk, AUDIO_BLOCK_SAMPLES);

        /* Current sequence ended inside this block: chain the next request */
        while ((n < AUDIO_BLOCK_SAMPLES) && status.playing)
        {
            finish();

            if (pdTRUE != xQueueReceive(queue, &m, 0U))
            {
                draining = true;            /* nothing queued */
            }
            else if (CMD_PLAY == m.cmd)
            {
                audio_source_start(&source, &m.seq, AUDIO_SAMPLE_RATE_HZ, AUDIO_WORD_GAP_MS);
                status.playing = true;
                n += audio_source_read(&source, &blk[n], AUDIO_BLOCK_SAMPLES - n);
            }
            else
            {
                (void)xQueueReset(queue);   /* STOP */
                draining = true;
            }
        }

        if (n < AUDIO_BLOCK_SAMPLES)
        {
            memset(&blk[n], 0, (AUDIO_BLOCK_SAMPLES - n) * sizeof(int16_t));
        }
        audio_i2s_commit_block(blk);
        status.underruns = audio_i2s_underruns();
    }
}


cy_rslt_t audio_player_init(CySCB_Type* base, cy_stc_scb_i2c_context_t* context)
{
    if ((NULL == base) || (NULL == context))
    {
        return CY_RSLT_TYPE_ERROR;
    }

    if (NULL != task_handle)
    {
        return CY_RSLT_SUCCESS;
    }

    i2c_base = base;
    i2c_ctx = context;
    status.volume = volume;

    queue = xQueueCreateStatic(AUDIO_QUEUE_LENGTH, sizeof(audio_msg_t), queue_storage, &queue_tcb);
    if (NULL == queue)
    {
        return CY_RSLT_TYPE_ERROR;
    }

    task_handle = xTaskCreateStatic(audio_task, "Audio Task", AUDIO_TASK_STACK_WORDS, NULL,
                                    AUDIO_TASK_PRIORITY, task_stack, &task_tcb);

    return (NULL != task_handle) ? CY_RSLT_SUCCESS : CY_RSLT_TYPE_ERROR;
}


bool audio_play(const audio_seq_t* seq, audio_prio_t prio)
{
    audio_msg_t m;
    BaseType_t ok;

    if ((NULL == queue) || (NULL == seq) || (0U == seq->count))
    {
        return false;
    }

    m.cmd = CMD_PLAY;
    m.prio = (uint8_t)prio;
    m.seq = *seq;

    ok = (AUDIO_PRIO_URGENT == prio) ? xQueueSendToFront(queue, &m, 0U)
                                     : xQueueSendToBack(queue, &m, 0U);
    if (pdTRUE != ok)
    {
        status.dropped++;
    }
    return (pdTRUE == ok);
}


bool audio_play_tone(audio_tone_t tone, audio_prio_t prio)
{
    audio_seq_t q;
    audio_seq_clear(&q);
    (void)audio_seq_add(&q, AUDIO_SEG_TONE, (uint16_t)tone);
    return audio_play(&q, prio);
}


bool audio_play_clip(audio_clip_id_t clip, audio_prio_t prio)
{
    audio_seq_t q;
    audio_seq_clear(&q);
    (void)audio_seq_add(&q, AUDIO_SEG_CLIP, (uint16_t)clip);
    return audio_play(&q, prio);
}


bool audio_play_tone_and_clip(audio_tone_t tone, audio_clip_id_t clip, audio_prio_t prio)
{
    audio_seq_t q;
    audio_seq_clear(&q);
    (void)audio_seq_add(&q, AUDIO_SEG_TONE, (uint16_t)tone);
    (void)audio_seq_add(&q, AUDIO_SEG_CLIP, (uint16_t)clip);
    return audio_play(&q, prio);
}


bool audio_say_speed_kmh(uint32_t kmh)
{
    audio_seq_t q;
    audio_seq_clear(&q);
    return audio_seq_speed_kmh(&q, kmh) && audio_play(&q, AUDIO_PRIO_NORMAL);
}


bool audio_say_altitude_m(int32_t m)
{
    audio_seq_t q;
    audio_seq_clear(&q);
    return audio_seq_altitude_m(&q, m) && audio_play(&q, AUDIO_PRIO_NORMAL);
}


bool audio_say_battery_pct(uint32_t pct)
{
    audio_seq_t q;
    audio_seq_clear(&q);
    return audio_seq_battery_pct(&q, pct) && audio_play(&q, AUDIO_PRIO_NORMAL);
}


bool audio_say_heading_deg(uint32_t deg)
{
    audio_seq_t q;
    audio_seq_clear(&q);
    return audio_seq_heading_deg(&q, deg) && audio_play(&q, AUDIO_PRIO_NORMAL);
}


void audio_stop(void)
{
    audio_msg_t m;

    if (NULL == queue)
    {
        return;
    }

    memset(&m, 0, sizeof(m));
    m.cmd = CMD_STOP;
    m.prio = (uint8_t)AUDIO_PRIO_URGENT;
    if (pdTRUE != xQueueSendToFront(queue, &m, 0U))
    {
        status.dropped++;
    }
}


void audio_set_volume(uint8_t level)
{
    volume = (level > AUDIO_VOLUME_MAX) ? (uint8_t)AUDIO_VOLUME_MAX : level;
    status.volume = volume;
    volume_dirty = true;
}


uint8_t audio_get_volume(void)
{
    return volume;
}


void audio_player_get_status(audio_player_status_t* st)
{
    if (NULL != st)
    {
        taskENTER_CRITICAL();
        memcpy(st, (const void*)&status, sizeof(*st));
        taskEXIT_CRITICAL();
    }
}

/* [] END OF FILE */

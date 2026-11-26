/*******************************************************************************
* File Name        : stt_link_mic_hook.c
*
* Description      : Glue to the 4-mic capture (PR #5, proj_cm55/mic): feeds
*                    the left PDM mic (D-L, 16 kHz, 10 ms frames) into the PTT
*                    recorder. Leave this file out if you use another audio
*                    source (e.g. the Audio Enhancement output) and call
*                    stt_link_push_pcm() from there instead.
*******************************************************************************/
#include "mic_task.h"
#include "stt_link.h"

#define MIC_FRAME_SAMPLES   (160U)

void mic_task_frame_hook(const int16_t *const ch[4], const bool valid[4])
{
    if (valid[0])
    {
        stt_link_push_pcm(ch[0], MIC_FRAME_SAMPLES);
    }
}

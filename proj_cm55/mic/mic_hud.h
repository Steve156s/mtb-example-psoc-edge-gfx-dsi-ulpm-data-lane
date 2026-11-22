/*******************************************************************************
* File Name        : mic_hud.h
*
* Description      : Draws the 4-channel microphone VU panel into an RGB565
*                    frame buffer. Portable (no PDL), so the same code renders
*                    a preview on a PC (tools/mic_host_test).
*
*******************************************************************************/
#ifndef MIC_HUD_H
#define MIC_HUD_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define MIC_HUD_CHANNELS        (4U)
#define MIC_HUD_WIDTH           (400U)
#define MIC_HUD_HEIGHT          (150U)

typedef enum
{
    MIC_HUD_CH_OK = 0,          /* channel running */
    MIC_HUD_CH_OFF,             /* channel stopped (e.g. PDM asleep in wake mode) */
    MIC_HUD_CH_ERROR            /* init failed / not built */
} mic_hud_ch_status_t;

typedef struct
{
    const char         *label;  /* e.g. "D-L" (max 3 chars) */
    mic_hud_ch_status_t status;
    uint32_t            vu_fill;    /* 0..MIC_HUD_BAR_W */
    uint32_t            hold_fill;  /* 0..MIC_HUD_BAR_W */
    int16_t             rms_db10;   /* printed as -xx dB */
    bool                clipping;   /* clipped during the last ~second */
} mic_hud_channel_t;

typedef struct
{
    mic_hud_channel_t ch[MIC_HUD_CHANNELS];
    bool              wake_mode;    /* PDM sleeps until analog VAD fires */
    bool              vad_active;   /* sound activity on the analog mics */
    int16_t           floor_db10;   /* analog noise floor */
} mic_hud_view_t;

/* Width of a VU bar in pixels (use as 'scale' for mic_level_vu_fill) */
#define MIC_HUD_BAR_W           (240U)

/* Draws the panel with its top-left corner at (x, y).
 *  fb      RGB565 frame buffer
 *  stride  frame buffer width in pixels
 *  fb_w/h  visible size (drawing is clipped to it) */
void mic_hud_draw(uint16_t *fb, uint32_t stride, uint32_t fb_w, uint32_t fb_h,
                  uint32_t x, uint32_t y, const mic_hud_view_t *view);

#if defined(__cplusplus)
}
#endif

#endif /* MIC_HUD_H */

/* [] END OF FILE */

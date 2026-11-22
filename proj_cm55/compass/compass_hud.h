/*******************************************************************************
* File Name        : compass_hud.h
*
* Description      : Draws a HUD style compass heading tape into an RGB565
*                    frame buffer region (top band of the screen).
*
*                    Pure C (no PDL / RTOS dependency) so the rendering can be
*                    previewed on a host PC.
*
*******************************************************************************/
#ifndef COMPASS_HUD_H
#define COMPASS_HUD_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Height of the band (rows from the top of the frame buffer) the HUD owns */
#define COMPASS_HUD_BAND_HEIGHT     (120U)

typedef enum
{
    COMPASS_HUD_MODE_WAIT = 0,      /* sensor starting                      */
    COMPASS_HUD_MODE_HEADING,       /* normal heading display               */
    COMPASS_HUD_MODE_CALIBRATING,   /* figure-8 calibration in progress     */
    COMPASS_HUD_MODE_NO_SENSOR,     /* BMM350 not found                     */
    COMPASS_HUD_MODE_ERROR          /* sensor / bus error                   */
} compass_hud_mode_t;

typedef struct
{
    compass_hud_mode_t mode;
    float heading_deg;
    uint8_t cal_progress;           /* 0..100                               */
    bool cal_valid;                 /* false => show "CAL?" hint            */
    bool tilt_compensated;          /* false => show "LEVEL" hint           */
    bool interference;              /* true  => show "MAG?" warning         */
} compass_hud_view_t;

/*
 * fb        : start of the RGB565 frame buffer (top-left pixel)
 * stride_px : frame buffer line length in pixels
 * width_px  : visible width in pixels (<= stride_px)
 * Draws rows [0, COMPASS_HUD_BAND_HEIGHT).
 */
void compass_hud_draw(uint16_t *fb, uint32_t stride_px, uint32_t width_px,
                      const compass_hud_view_t *view);

#if defined(__cplusplus)
}
#endif

#endif /* COMPASS_HUD_H */

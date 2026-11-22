/*******************************************************************************
* File Name        : goggle_link_hud.h
*
* Description      : Draws the "LINK" panel (Bluetooth / Wi-Fi state, Tracker
*                    GNSS, nearest team members, last message or pairing
*                    passkey) into an RGB565 frame buffer. Hardware
*                    independent (5x7 bitmap font, integer scaling); the
*                    caller flushes the D-cache.
*
*******************************************************************************/
#ifndef GOGGLE_LINK_HUD_H
#define GOGGLE_LINK_HUD_H

#include <stdbool.h>
#include <stdint.h>

#include "goggle_link_shm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define GL_HUD_RGB565(r, g, b)  ((uint16_t)((((r) & 0xF8U) << 8) | \
                                            (((g) & 0xFCU) << 3) | \
                                            ((b) >> 3)))

#define GL_HUD_SCALE            (2U)
#define GL_HUD_COLS             (20U)
#define GL_HUD_LINES            (8U)
#define GL_HUD_PAD              (8U)
#define GL_HUD_CHAR_ADV         (6U * GL_HUD_SCALE)
#define GL_HUD_LINE_ADV         (9U * GL_HUD_SCALE)
#define GL_HUD_WIDTH            ((GL_HUD_COLS * GL_HUD_CHAR_ADV) + (2U * GL_HUD_PAD))
#define GL_HUD_HEIGHT           ((GL_HUD_LINES * GL_HUD_LINE_ADV) + (2U * GL_HUD_PAD))

/* Data older than this is shown as stale */
#define GL_HUD_GNSS_STALE_MS    (5000U)
#define GL_HUD_TEAM_STALE_MS    (30000U)

/* Number of team rows on the panel (SOS first, then nearest) */
#define GL_HUD_TEAM_ROWS        (2U)

/* Draws the panel with its top-left corner at (x, y).
 * fb: RGB565 frame buffer, stride: pixels per line, fb_height: lines.
 * d: latest mailbox snapshot, or NULL when the CM33 side is not running.
 * The panel is GL_HUD_WIDTH x GL_HUD_HEIGHT pixels, clipped to the buffer. */
void gl_hud_draw(uint16_t* fb, uint32_t stride, uint32_t fb_height,
                 uint32_t x, uint32_t y, const gl_down_t* d);

/* Text of one panel line as it would be drawn (for logging / unit tests).
 * line: 0..GL_HUD_LINES-1. Returns false for an invalid line. */
bool gl_hud_line_text(const gl_down_t* d, uint32_t line, char* out, uint32_t cap);

#if defined(__cplusplus)
}
#endif

#endif /* GOGGLE_LINK_HUD_H */

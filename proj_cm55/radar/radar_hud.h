/*******************************************************************************
* File Name        : radar_hud.h
*
* Description      : HUD page model switched by radar gestures, and a small
*                    RGB565 panel renderer (5x7 font, x3) for the 832x480
*                    frame buffer of this example. Platform independent.
*
*                    Pages (placeholders until the sensor PRs are merged):
*                      1 SPEED  speed / altitude / time
*                      2 NAV    heading / waypoint / distance
*                      3 TEAM   LoRa peers / nearest / SOS
*                      4 RADAR  live radar debug (range, azimuth, elevation)
*
*                    Gesture mapping:
*                      SWIPE RIGHT -> next page      SWIPE LEFT -> previous
*                      SWIPE UP    -> home (SPEED)   SWIPE DOWN -> RADAR page
*                      PUSH        -> select (in this demo: DSI ULPM 6 s)
*
*******************************************************************************/
#ifndef RADAR_HUD_H
#define RADAR_HUD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "radar_gesture.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define RADAR_HUD_RGB565(r, g, b)   ((uint16_t)((((r) & 0xF8U) << 8) | \
                                                (((g) & 0xFCU) << 3) | \
                                                (((b) & 0xF8U) >> 3)))

#define RADAR_HUD_SCALE             (3U)
#define RADAR_HUD_COLS              (16U)
#define RADAR_HUD_LINES             (5U)
#define RADAR_HUD_PAD               (8U)
#define RADAR_HUD_CHAR_ADV          (6U * RADAR_HUD_SCALE)
#define RADAR_HUD_LINE_ADV          (9U * RADAR_HUD_SCALE)
#define RADAR_HUD_WIDTH             ((RADAR_HUD_COLS * RADAR_HUD_CHAR_ADV) + (2U * RADAR_HUD_PAD))
#define RADAR_HUD_HEIGHT            ((RADAR_HUD_LINES * RADAR_HUD_LINE_ADV) + (2U * RADAR_HUD_PAD))

typedef enum
{
    HUD_PAGE_SPEED = 0,
    HUD_PAGE_NAV,
    HUD_PAGE_TEAM,
    HUD_PAGE_RADAR,
    HUD_PAGE_COUNT
} hud_page_t;

typedef enum
{
    RADAR_HUD_MODE_WAIT = 0,    /* radar starting                     */
    RADAR_HUD_MODE_OK,          /* frames are being processed         */
    RADAR_HUD_MODE_NO_RADAR,    /* BSP has no radar (not KIT_PSE84_AI) */
    RADAR_HUD_MODE_ERROR        /* SPI / sensor error                 */
} radar_hud_mode_t;

typedef struct
{
    hud_page_t       page;
    radar_hud_mode_t mode;
    bool             toast;         /* show the last gesture banner   */
    radar_gesture_t  last_gesture;
    bool             present;       /* hand currently detected        */
    float            range_cm;
    float            azimuth_deg;
    float            elevation_deg;
} radar_hud_view_t;

/** Page after applying a gesture (PUSH and NONE keep the page). */
hud_page_t radar_hud_next_page(hud_page_t page, radar_gesture_t g);

const char* radar_hud_page_name(hud_page_t page);

/**
 * Draw the panel (RADAR_HUD_WIDTH x RADAR_HUD_HEIGHT) at (x, y) into an
 * RGB565 frame buffer with the given stride (pixels) and height.
 */
void radar_hud_draw(uint16_t* fb, uint32_t stride, uint32_t fb_height,
                    uint32_t x, uint32_t y, const radar_hud_view_t* view);

/**
 * Fixed-point formatting without printf float support (newlib-nano).
 * Returns the number of characters written (excluding '\0').
 */
size_t radar_format_fixed(char* buf, size_t size, float value,
                          uint32_t decimals, bool plus_sign);

#if defined(__cplusplus)
}
#endif

#endif /* RADAR_HUD_H */

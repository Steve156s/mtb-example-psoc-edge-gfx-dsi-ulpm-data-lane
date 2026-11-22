/*******************************************************************************
* File Name        : radar_gesture.c
*
* Description      : See radar_gesture.h
*
*******************************************************************************/
#include <math.h>
#include <string.h>

#include "radar_config.h"
#include "radar_gesture.h"


static float edge_mean(const float* v, uint32_t n, bool from_end)
{
    uint32_t m = (n < RADAR_GESTURE_EDGE_FRAMES) ? n : RADAR_GESTURE_EDGE_FRAMES;
    float sum = 0.0f;

    if (0U == m)
    {
        return 0.0f;
    }
    for (uint32_t i = 0U; i < m; i++)
    {
        sum += from_end ? v[n - 1U - i] : v[i];
    }
    return sum / (float)m;
}


static radar_gesture_t classify(radar_gesture_ctx_t* ctx)
{
    uint32_t n = ctx->n;
    float d_az;
    float d_el;
    float r_start;
    float r_min;
    float mean_abs_az = 0.0f;
    float mean_abs_el = 0.0f;
    radar_gesture_t g = RADAR_GESTURE_NONE;

    d_az = edge_mean(ctx->az, n, true) - edge_mean(ctx->az, n, false);
    d_el = edge_mean(ctx->el, n, true) - edge_mean(ctx->el, n, false);
    r_start = edge_mean(ctx->rng, n, false);

    r_min = r_start;
    for (uint32_t i = 0U; i < n; i++)
    {
        if (ctx->rng[i] < r_min)
        {
            r_min = ctx->rng[i];
        }
        mean_abs_az += fabsf(ctx->az[i]);
        mean_abs_el += fabsf(ctx->el[i]);
    }
    if (n > 0U)
    {
        mean_abs_az /= (float)n;
        mean_abs_el /= (float)n;
    }

    ctx->last_d_az = d_az;
    ctx->last_d_el = d_el;
    ctx->last_d_rng = r_min - r_start;
    ctx->last_frames = n;

    if (n < RADAR_GESTURE_MIN_FRAMES)
    {
        return RADAR_GESTURE_NONE;
    }

    if ((fabsf(d_az) >= RADAR_SWIPE_MIN_DEG) &&
        (fabsf(d_az) >= (RADAR_SWIPE_DOMINANCE * fabsf(d_el))))
    {
        g = (d_az > 0.0f) ? RADAR_GESTURE_SWIPE_RIGHT : RADAR_GESTURE_SWIPE_LEFT;
    }
    else if ((fabsf(d_el) >= RADAR_SWIPE_MIN_DEG) &&
             (fabsf(d_el) >= (RADAR_SWIPE_DOMINANCE * fabsf(d_az))))
    {
        g = (d_el > 0.0f) ? RADAR_GESTURE_SWIPE_UP : RADAR_GESTURE_SWIPE_DOWN;
    }
    else if (((r_start - r_min) >= RADAR_PUSH_MIN_CM) &&
             (mean_abs_az <= RADAR_PUSH_MAX_DEG) &&
             (mean_abs_el <= RADAR_PUSH_MAX_DEG))
    {
        g = RADAR_GESTURE_PUSH;
    }

    return g;
}


void radar_gesture_init(radar_gesture_ctx_t* ctx)
{
    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->state = RADAR_TRACK_IDLE;
}


bool radar_gesture_is_tracking(const radar_gesture_ctx_t* ctx)
{
    return (RADAR_TRACK_ARMING == ctx->state) || (RADAR_TRACK_ACTIVE == ctx->state);
}


radar_clutter_mode_t radar_gesture_clutter_mode(const radar_gesture_ctx_t* ctx)
{
    switch (ctx->state)
    {
        case RADAR_TRACK_ARMING:
        case RADAR_TRACK_ACTIVE:
            return RADAR_CLUTTER_FREEZE;
        case RADAR_TRACK_COOLDOWN:
            return RADAR_CLUTTER_ABSORB;
        case RADAR_TRACK_IDLE:
        default:
            return RADAR_CLUTTER_LEARN;
    }
}


static void push_sample(radar_gesture_ctx_t* ctx, const radar_frame_result_t* f)
{
    if (ctx->n < RADAR_GESTURE_HISTORY)
    {
        ctx->az[ctx->n]  = f->azimuth_deg;
        ctx->el[ctx->n]  = f->elevation_deg;
        ctx->rng[ctx->n] = f->range_cm;
        ctx->n++;
    }
}


radar_gesture_t radar_gesture_update(radar_gesture_ctx_t* ctx,
                                     const radar_frame_result_t* frame)
{
    radar_gesture_t g = RADAR_GESTURE_NONE;

    switch (ctx->state)
    {
        case RADAR_TRACK_COOLDOWN:
            if (ctx->cooldown > 0U)
            {
                ctx->cooldown--;
            }
            /* wait until the hand has left before arming again */
            if ((0U == ctx->cooldown) && !frame->present)
            {
                ctx->state = RADAR_TRACK_IDLE;
            }
            break;

        case RADAR_TRACK_IDLE:
            if (frame->present)
            {
                ctx->n = 0U;
                ctx->on_count = 1U;
                ctx->off_count = 0U;
                push_sample(ctx, frame);
                ctx->state = (RADAR_GESTURE_ON_FRAMES <= 1U) ? RADAR_TRACK_ACTIVE : RADAR_TRACK_ARMING;
            }
            break;

        case RADAR_TRACK_ARMING:
            if (frame->present)
            {
                push_sample(ctx, frame);
                ctx->on_count++;
                if (ctx->on_count >= RADAR_GESTURE_ON_FRAMES)
                {
                    ctx->state = RADAR_TRACK_ACTIVE;
                }
            }
            else
            {
                /* single-frame blip: discard */
                ctx->n = 0U;
                ctx->state = RADAR_TRACK_IDLE;
            }
            break;

        case RADAR_TRACK_ACTIVE:
        default:
            if (frame->present)
            {
                ctx->off_count = 0U;
                push_sample(ctx, frame);
            }
            else
            {
                ctx->off_count++;
            }

            if ((ctx->off_count >= RADAR_GESTURE_OFF_FRAMES) ||
                (ctx->n >= RADAR_GESTURE_MAX_FRAMES))
            {
                g = classify(ctx);
                ctx->cooldown = RADAR_GESTURE_COOLDOWN_FRAMES;
                ctx->state = RADAR_TRACK_COOLDOWN;
            }
            break;
    }

    return g;
}


const char* radar_gesture_name(radar_gesture_t g)
{
    switch (g)
    {
        case RADAR_GESTURE_SWIPE_LEFT:  return "SWIPE LEFT";
        case RADAR_GESTURE_SWIPE_RIGHT: return "SWIPE RIGHT";
        case RADAR_GESTURE_SWIPE_UP:    return "SWIPE UP";
        case RADAR_GESTURE_SWIPE_DOWN:  return "SWIPE DOWN";
        case RADAR_GESTURE_PUSH:        return "PUSH";
        case RADAR_GESTURE_NONE:
        default:                        return "NONE";
    }
}

/* [] END OF FILE */

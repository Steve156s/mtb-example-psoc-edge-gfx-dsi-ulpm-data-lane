/*******************************************************************************
* File Name        : compass_math.c
*
* Description      : Hardware independent e-compass math. See compass_math.h.
*
*******************************************************************************/
#include <math.h>
#include <float.h>
#include <stddef.h>

#include "compass_math.h"

#define COMPASS_PI_F        (3.14159265358979f)
#define RAD_TO_DEG_F        (180.0f / COMPASS_PI_F)
#define DEG_TO_RAD_F        (COMPASS_PI_F / 180.0f)

static compass_vec3_t vec_cross(const compass_vec3_t *a, const compass_vec3_t *b)
{
    compass_vec3_t r;
    r.x = (a->y * b->z) - (a->z * b->y);
    r.y = (a->z * b->x) - (a->x * b->z);
    r.z = (a->x * b->y) - (a->y * b->x);
    return r;
}

static float vec_dot(const compass_vec3_t *a, const compass_vec3_t *b)
{
    return (a->x * b->x) + (a->y * b->y) + (a->z * b->z);
}

float compass_vec3_norm(const compass_vec3_t *v)
{
    return sqrtf(vec_dot(v, v));
}

float compass_wrap_360(float deg)
{
    float r = fmodf(deg, 360.0f);
    if (r < 0.0f)
    {
        r += 360.0f;
    }
    if (r >= 360.0f)
    {
        r -= 360.0f;
    }
    return r;
}

void compass_cal_reset(compass_cal_t *cal)
{
    cal->offset.x = 0.0f;
    cal->offset.y = 0.0f;
    cal->offset.z = 0.0f;
    cal->scale.x = 1.0f;
    cal->scale.y = 1.0f;
    cal->scale.z = 1.0f;
    cal->valid = false;
}

void compass_cal_collector_start(compass_cal_collector_t *col)
{
    col->min.x = FLT_MAX;
    col->min.y = FLT_MAX;
    col->min.z = FLT_MAX;
    col->max.x = -FLT_MAX;
    col->max.y = -FLT_MAX;
    col->max.z = -FLT_MAX;
    col->samples = 0U;
}

void compass_cal_collector_add(compass_cal_collector_t *col, const compass_vec3_t *m)
{
    col->min.x = fminf(col->min.x, m->x);
    col->min.y = fminf(col->min.y, m->y);
    col->min.z = fminf(col->min.z, m->z);
    col->max.x = fmaxf(col->max.x, m->x);
    col->max.y = fmaxf(col->max.y, m->y);
    col->max.z = fmaxf(col->max.z, m->z);
    col->samples++;
}

uint8_t compass_cal_collector_progress(const compass_cal_collector_t *col)
{
    float rx, ry, rz, p;

    if (col->samples < 2U)
    {
        return 0U;
    }

    rx = fminf((col->max.x - col->min.x) / COMPASS_CAL_MIN_RANGE_UT, 1.0f);
    ry = fminf((col->max.y - col->min.y) / COMPASS_CAL_MIN_RANGE_UT, 1.0f);
    rz = fminf((col->max.z - col->min.z) / COMPASS_CAL_MIN_RANGE_UT, 1.0f);

    p = (rx + ry + rz) * (100.0f / 3.0f);
    return (uint8_t) fminf(p, 100.0f);
}

bool compass_cal_collector_finish(const compass_cal_collector_t *col, compass_cal_t *cal)
{
    compass_vec3_t range;
    float avg;

    if (col->samples < 50U)
    {
        return false;
    }

    range.x = col->max.x - col->min.x;
    range.y = col->max.y - col->min.y;
    range.z = col->max.z - col->min.z;

    if ((range.x < COMPASS_CAL_MIN_RANGE_UT) ||
        (range.y < COMPASS_CAL_MIN_RANGE_UT) ||
        (range.z < COMPASS_CAL_MIN_RANGE_UT))
    {
        return false;
    }

    /* Hard iron: centre of the min/max box */
    cal->offset.x = (col->max.x + col->min.x) * 0.5f;
    cal->offset.y = (col->max.y + col->min.y) * 0.5f;
    cal->offset.z = (col->max.z + col->min.z) * 0.5f;

    /* Soft iron (diagonal approximation): stretch the ellipsoid to a sphere */
    avg = (range.x + range.y + range.z) / 3.0f;
    cal->scale.x = avg / range.x;
    cal->scale.y = avg / range.y;
    cal->scale.z = avg / range.z;

    cal->valid = true;
    return true;
}

compass_vec3_t compass_cal_apply(const compass_cal_t *cal, const compass_vec3_t *m_raw)
{
    compass_vec3_t r;
    r.x = (m_raw->x - cal->offset.x) * cal->scale.x;
    r.y = (m_raw->y - cal->offset.y) * cal->scale.y;
    r.z = (m_raw->z - cal->offset.z) * cal->scale.z;
    return r;
}

/*
 * Frame-independent formulation (same as Android SensorManager
 * getRotationMatrix): with "up" U = acc and magnetic field M,
 *   East  E = M x U
 *   North N = U x E
 * Both lie in the horizontal plane, so the heading of the forward axis F is
 * atan2(F.E, F.N). Works for any right-handed body frame.
 */
float compass_tilt_compensated_heading(const compass_vec3_t *acc,
                                       const compass_vec3_t *mag,
                                       const compass_vec3_t *forward)
{
    static const compass_vec3_t level_up = { 0.0f, 0.0f, 1.0f };
    const compass_vec3_t *up = (NULL != acc) ? acc : &level_up;
    compass_vec3_t east, north;
    float up_norm, east_norm, north_norm, fe, fn;

    up_norm = compass_vec3_norm(up);
    if (up_norm < 1e-6f)
    {
        return -1.0f;  /* free fall */
    }

    east = vec_cross(mag, up);
    east_norm = compass_vec3_norm(&east);
    if (east_norm < (1e-3f * up_norm * fmaxf(compass_vec3_norm(mag), 1e-6f)))
    {
        return -1.0f;  /* field parallel to gravity (near magnetic pole) */
    }

    north = vec_cross(up, &east);
    north_norm = compass_vec3_norm(&north);

    fe = vec_dot(forward, &east) / east_norm;
    fn = vec_dot(forward, &north) / north_norm;

    if ((fabsf(fe) < 1e-6f) && (fabsf(fn) < 1e-6f))
    {
        return -1.0f;  /* forward axis points straight up / down */
    }

    return compass_wrap_360(atan2f(fe, fn) * RAD_TO_DEG_F);
}

float compass_heading_filter_update(compass_heading_filter_t *f, float heading_deg, float alpha)
{
    float rad = heading_deg * DEG_TO_RAD_F;
    float s = sinf(rad);
    float c = cosf(rad);

    if ((!f->primed) || (alpha >= 1.0f))
    {
        f->s = s;
        f->c = c;
        f->primed = true;
    }
    else
    {
        f->s += alpha * (s - f->s);
        f->c += alpha * (c - f->c);
    }

    return compass_wrap_360(atan2f(f->s, f->c) * RAD_TO_DEG_F);
}

const char *compass_cardinal_8(float heading_deg)
{
    static const char *const names[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    uint32_t idx = (uint32_t) ((compass_wrap_360(heading_deg) + 22.5f) / 45.0f) % 8U;
    return names[idx];
}

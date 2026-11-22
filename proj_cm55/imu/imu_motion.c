/*******************************************************************************
* File Name        : imu_motion.c
*
* Description      : Hardware-independent pitch/roll filter and fall / impact
*                    detector. See imu_motion.h.
*
*******************************************************************************/
#include "imu_motion.h"

#include <math.h>
#include <stddef.h>

/*******************************************************************************
* Macros
*******************************************************************************/
#define RAD_TO_DEG              (57.29577951f)

/* Free fall that ended less than this before an impact counts as "fell". */
#define FREEFALL_TO_IMPACT_MS   (1000U)

/* Time constant of the pre-impact reference attitude low-pass (seconds). */
#define REF_ATTITUDE_TAU_S      (1.0f)

/*******************************************************************************
* Local helpers
*******************************************************************************/
static float wrap_180(float deg)
{
    while (deg > 180.0f)
    {
        deg -= 360.0f;
    }
    while (deg < -180.0f)
    {
        deg += 360.0f;
    }
    return deg;
}

static float vec_norm(const float v[3])
{
    return sqrtf((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
}

/* Elapsed time with wrap-around safe arithmetic. */
static uint32_t elapsed_ms(uint32_t now_ms, uint32_t since_ms)
{
    return (uint32_t)(now_ms - since_ms);
}

static void update_attitude(imu_motion_t *m, const float a[3],
                            const float g[3], float dt_s)
{
    const imu_motion_cfg_t *cfg = &m->cfg;

    /* Tilt from gravity */
    float acc_roll  = atan2f(a[1], a[2]) * RAD_TO_DEG;
    float acc_pitch = atan2f(-a[0], sqrtf((a[1] * a[1]) + (a[2] * a[2])))
                      * RAD_TO_DEG;

    bool accel_ok = (m->accel_norm_g >= cfg->accel_trust_min_g) &&
                    (m->accel_norm_g <= cfg->accel_trust_max_g);

    if (!m->attitude_valid)
    {
        if (accel_ok)
        {
            m->roll_deg       = acc_roll;
            m->pitch_deg      = acc_pitch;
            m->ref_roll_deg   = acc_roll;
            m->ref_pitch_deg  = acc_pitch;
            m->attitude_valid = true;
        }
        return;
    }

    /* Propagate with gyro (small-angle approximation) */
    float roll_gyro  = wrap_180(m->roll_deg + (g[0] * dt_s));
    float pitch_gyro = m->pitch_deg + (g[1] * dt_s);

    if (accel_ok)
    {
        float alpha = cfg->filter_alpha;
        /* Blend on the wrapped difference so +179/-179 does not jump */
        m->roll_deg  = wrap_180(roll_gyro +
                                ((1.0f - alpha) * wrap_180(acc_roll - roll_gyro)));
        m->pitch_deg = pitch_gyro + ((1.0f - alpha) * (acc_pitch - pitch_gyro));
    }
    else
    {
        /* Linear acceleration present: trust gyro only */
        m->roll_deg  = roll_gyro;
        m->pitch_deg = pitch_gyro;
    }

    if (m->pitch_deg > 90.0f)
    {
        m->pitch_deg = 90.0f;
    }
    else if (m->pitch_deg < -90.0f)
    {
        m->pitch_deg = -90.0f;
    }
}

/*******************************************************************************
* Public API
*******************************************************************************/
void imu_motion_default_cfg(imu_motion_cfg_t *cfg)
{
    if (NULL == cfg)
    {
        return;
    }
    cfg->filter_alpha       = 0.98f;
    cfg->accel_trust_min_g  = 0.80f;
    cfg->accel_trust_max_g  = 1.20f;

    cfg->freefall_g         = 0.35f;
    cfg->freefall_min_ms    = 80U;

    cfg->impact_g           = 3.5f;

    cfg->fall_window_ms     = 4000U;
    cfg->still_min_ms       = 1500U;
    cfg->still_gyro_dps     = 30.0f;
    cfg->still_accel_dev_g  = 0.15f;
    cfg->fall_tilt_deg      = 45.0f;
}

void imu_motion_init(imu_motion_t *m, const imu_motion_cfg_t *cfg)
{
    if (NULL == m)
    {
        return;
    }
    *m = (imu_motion_t){ 0 };
    if (NULL != cfg)
    {
        m->cfg = *cfg;
    }
    else
    {
        imu_motion_default_cfg(&m->cfg);
    }
}

uint32_t imu_motion_update(imu_motion_t *m, const float accel_g[3],
                           const float gyro_dps[3], float dt_s,
                           uint32_t now_ms)
{
    uint32_t events = IMU_EVENT_NONE;
    const imu_motion_cfg_t *cfg;

    if ((NULL == m) || (NULL == accel_g) || (NULL == gyro_dps))
    {
        return IMU_EVENT_NONE;
    }
    cfg = &m->cfg;

    if ((dt_s <= 0.0f) || (dt_s > 0.5f))
    {
        dt_s = 0.0f;   /* first sample or a long gap: do not integrate */
    }

    m->accel_norm_g  = vec_norm(accel_g);
    m->gyro_norm_dps = vec_norm(gyro_dps);

    update_attitude(m, accel_g, gyro_dps, dt_s);

    /* ---------------- Free fall ---------------- */
    if (m->accel_norm_g < cfg->freefall_g)
    {
        if (!m->in_freefall)
        {
            m->in_freefall       = true;
            m->freefall_reported = false;
            m->freefall_start_ms = now_ms;
        }
        else if (!m->freefall_reported &&
                 (elapsed_ms(now_ms, m->freefall_start_ms) >= cfg->freefall_min_ms))
        {
            m->freefall_reported = true;
            events |= IMU_EVENT_FREEFALL;
        }
        if (m->freefall_reported)
        {
            m->last_freefall_ms = now_ms;
        }
    }
    else
    {
        m->in_freefall = false;
    }

    /* ---------------- Impact ---------------- */
    if (m->accel_norm_g >= cfg->impact_g)
    {
        if (!m->impact_active)
        {
            /* Rising edge: new impact */
            m->impact_active = true;
            m->impact_peak_g = m->accel_norm_g;
            m->impact_ms     = now_ms;
            m->fall_watch    = true;
            m->is_still      = false;
            m->impact_after_freefall =
                (0U != m->last_freefall_ms) &&
                (elapsed_ms(now_ms, m->last_freefall_ms) <= FREEFALL_TO_IMPACT_MS);
            events |= IMU_EVENT_IMPACT;
        }
        else if (m->accel_norm_g > m->impact_peak_g)
        {
            m->impact_peak_g = m->accel_norm_g;
        }
        m->last_impact_peak_g = m->impact_peak_g;
    }
    else
    {
        m->impact_active = false;
    }

    /* ---------------- Fall: impact followed by lying still ---------------- */
    if (m->fall_watch && !m->impact_active)
    {
        float dev = fabsf(m->accel_norm_g - 1.0f);
        bool still = (m->gyro_norm_dps < cfg->still_gyro_dps) &&
                     (dev < cfg->still_accel_dev_g);

        if (still)
        {
            if (!m->is_still)
            {
                m->is_still       = true;
                m->still_start_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, m->still_start_ms) >= cfg->still_min_ms)
            {
                float d_pitch = fabsf(m->pitch_deg - m->ref_pitch_deg);
                float d_roll  = fabsf(wrap_180(m->roll_deg - m->ref_roll_deg));
                float tilt    = (d_pitch > d_roll) ? d_pitch : d_roll;

                /* Upright and still after a hard landing (e.g. a jump)
                 * is not a fall; head / body must have tipped over. */
                if (tilt >= cfg->fall_tilt_deg)
                {
                    events |= IMU_EVENT_FALL;
                }
                m->fall_watch = false;   /* decided either way */
            }
        }
        else
        {
            m->is_still = false;
        }

        if (m->fall_watch &&
            (elapsed_ms(now_ms, m->impact_ms) > cfg->fall_window_ms))
        {
            m->fall_watch = false;       /* got up / kept moving: no fall */
        }
    }

    /* Track a slow reference attitude while nothing is happening, so the
     * fall check compares against the attitude from before the crash. */
    if (m->attitude_valid && !m->fall_watch && !m->impact_active &&
        (dt_s > 0.0f))
    {
        float k = dt_s / (REF_ATTITUDE_TAU_S + dt_s);
        m->ref_pitch_deg += k * (m->pitch_deg - m->ref_pitch_deg);
        m->ref_roll_deg   = wrap_180(m->ref_roll_deg +
                                     (k * wrap_180(m->roll_deg - m->ref_roll_deg)));
    }

    return events;
}

float imu_motion_last_impact_g(const imu_motion_t *m)
{
    return (NULL != m) ? m->last_impact_peak_g : 0.0f;
}

/*******************************************************************************
* File Name        : audio_demo.c
*
* Description      : Cycles through every tone prompt, voice phrase and
*                    spoken-number builder on each USER BTN2 press. The
*                    sensor values are fixed examples: the product reads them
*                    from the GNSS / barometer / fuel gauge / compass.
*
*******************************************************************************/
#include "audio_demo.h"
#include "audio_player.h"

const char* audio_demo_next(void)
{
    static unsigned step = 0U;
    const char* what;

    switch (step)
    {
        case 0:  (void)audio_say_speed_kmh(32U);    what = "speed 32 km/h";          break;
        case 1:  (void)audio_say_altitude_m(2350);  what = "altitude 2350 m";        break;
        case 2:  (void)audio_say_battery_pct(85U);  what = "battery 85 %";           break;
        case 3:  (void)audio_say_heading_deg(45U);  what = "heading 45 deg (NE)";    break;
        case 4:  (void)audio_play_tone_and_clip(AUDIO_TONE_LOW_BATTERY, AUDIO_CLIP_LOW_BATTERY, AUDIO_PRIO_NORMAL);
                 what = "low battery";            break;
        case 5:  (void)audio_play_tone_and_clip(AUDIO_TONE_CONFIRM, AUDIO_CLIP_TRACKER_CONNECTED, AUDIO_PRIO_NORMAL);
                 what = "tracker connected";      break;
        case 6:  (void)audio_play_tone_and_clip(AUDIO_TONE_WARNING, AUDIO_CLIP_LINK_LOST, AUDIO_PRIO_NORMAL);
                 what = "link lost";              break;
        case 7:  (void)audio_play_tone_and_clip(AUDIO_TONE_MESSAGE, AUDIO_CLIP_NEW_MESSAGE, AUDIO_PRIO_NORMAL);
                 what = "new message";            break;
        case 8:  (void)audio_play_tone(AUDIO_TONE_PTT_START, AUDIO_PRIO_NORMAL);
                 (void)audio_play_tone(AUDIO_TONE_PTT_END, AUDIO_PRIO_NORMAL);
                 what = "PTT start / end";        break;
        case 9:  (void)audio_play_tone_and_clip(AUDIO_TONE_WARNING, AUDIO_CLIP_FOG_WARNING, AUDIO_PRIO_NORMAL);
                 what = "fog warning";            break;
        case 10: (void)audio_play_tone_and_clip(AUDIO_TONE_CONFIRM, AUDIO_CLIP_BATTERY_HEATING, AUDIO_PRIO_NORMAL);
                 what = "battery heating";        break;
        case 11: (void)audio_play_tone_and_clip(AUDIO_TONE_CONFIRM, AUDIO_CLIP_COMPASS_CAL_DONE, AUDIO_PRIO_NORMAL);
                 what = "compass calibrated";     break;
        case 12: (void)audio_play_tone(AUDIO_TONE_ERROR, AUDIO_PRIO_NORMAL);
                 what = "error tone";             break;
        default: (void)audio_play_tone_and_clip(AUDIO_TONE_SOS, AUDIO_CLIP_SOS_SENT, AUDIO_PRIO_URGENT);
                 what = "SOS (urgent, cuts current prompt)";
                 step = (unsigned)-1;             break;
    }

    step++;
    return what;
}

/* [] END OF FILE */

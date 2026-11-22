/*******************************************************************************
* File Name        : audio_demo.h
*
* Description      : Button demo of the audio prompts (USER BTN2 cycles).
*
*******************************************************************************/
#ifndef AUDIO_DEMO_H
#define AUDIO_DEMO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Queues the next demo item and returns its description */
const char* audio_demo_next(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_DEMO_H */

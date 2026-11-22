/*******************************************************************************
* File Name        : radar_task.h
*
* Description      : FreeRTOS task that drives the on-board XENSIV BGT60TR13C
*                    60 GHz radar of KIT_PSE84_AI through the Infineon
*                    sensor-xensiv-bgt60trxx library (PSOC Edge port, PDL SCB
*                    SPI), runs radar_dsp + radar_gesture on every frame and
*                    publishes the recognized gestures.
*
*                    KIT_PSE84_AI wiring (schematic 630-60751-01 Rev03):
*                      SPI  MISO P21.4, MOSI P21.5, CLK P21.6, CS P21.7
*                      IRQ  P20.3 (FIFO fill level reached, rising edge)
*                      RST  P20.7 (SPI_DIO3)
*                      Supplies VCC_3V3 (NCP163 LDO) and VCC_1V8, 80 MHz osc.
*                    The pins come from the BSP (CYBSP_RSPI_*, CYBSP_RADAR_*,
*                    CYBSP_SPI_CONTROLLER_*). If the selected BSP has no radar
*                    (e.g. KIT_PSE84_EVAL) the task is not started and
*                    radar_task_start() returns RADAR_RSLT_NOT_AVAILABLE.
*
*******************************************************************************/
#ifndef RADAR_TASK_H
#define RADAR_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "cy_result.h"
#include "FreeRTOS.h"
#include "task.h"

#include "radar_dsp.h"
#include "radar_gesture.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Application specific result module id (any unused 14-bit value) */
#define RADAR_RSLT_MODULE           (0x3A0U)
#define RADAR_RSLT_NOT_AVAILABLE    (CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, RADAR_RSLT_MODULE, 1U))
#define RADAR_RSLT_TASK_FAILED      (CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, RADAR_RSLT_MODULE, 2U))

typedef enum
{
    RADAR_STATUS_INIT = 0,
    RADAR_STATUS_RUNNING,
    RADAR_STATUS_NOT_AVAILABLE,
    RADAR_STATUS_ERROR
} radar_status_t;

typedef struct
{
    radar_status_t       status;
    radar_frame_result_t frame;         /* latest frame                */
    uint32_t             frames;        /* frames processed            */
    uint32_t             fifo_errors;   /* FIFO overflow / SPI errors  */
    uint32_t             gestures;      /* gestures recognized         */
    radar_gesture_t      last_gesture;
    float                last_d_az;     /* details of the last track   */
    float                last_d_el;
    float                last_d_rng;
} radar_state_t;

/**
 * Initializes the radar and starts the task.
 * @param priority FreeRTOS priority. The FIFO holds 8192 samples and one
 *                 frame is 6144, so the frame must be read within ~20 ms of
 *                 the interrupt: run the task at a high priority.
 */
cy_rslt_t radar_task_start(UBaseType_t priority);

/** Copy of the latest state (thread safe). */
void radar_task_get_state(radar_state_t* state);

/** Pop the next recognized gesture. Returns false if none is pending. */
bool radar_task_get_gesture(radar_gesture_t* gesture);

/**
 * Called from the radar task for every recognized gesture. Weak, empty by
 * default: the product overrides it (e.g. send to the UI task / LVGL).
 */
void radar_port_on_gesture(radar_gesture_t gesture);

#if defined(__cplusplus)
}
#endif

#endif /* RADAR_TASK_H */

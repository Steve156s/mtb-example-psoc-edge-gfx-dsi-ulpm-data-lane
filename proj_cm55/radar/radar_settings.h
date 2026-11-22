/******************************************************************************
* File Name:   radar_settings.h
*
* Description: BGT60TR13C register configuration for hand gesture sensing.
*
*              This is the "gesture" preset (P1) of the Infineon code example
*              mtb-example-psoc-edge-ml-deepcraft-deploy-radar (KIT_PSE84_AI),
*              which is identical to the register list of
*              mtb-example-psoc6-radar-gestures. Register values are copied
*              unchanged; only the symbol names were made static/const.
*
*              58.5 - 62.5 GHz (4 GHz -> ~3.75 cm range bin), 64 samples per
*              chirp at 2 MS/s, 32 chirps per frame, 300 us chirp repetition,
*              3 RX antennas (RX1..RX3, samples interleaved), 1 TX,
*              30 ms frame period (~33 frames/s).
*              6144 samples (12 KB) per frame, FIFO size of the BGT60TR13C is
*              8192 samples.
*
*              To change the configuration, regenerate the register list with
*              the Infineon Radar Fusion GUI / BGT60 configurator and replace
*              this file.
*
*******************************************************************************
* Copyright 2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation. All rights reserved.
* Used under the Cypress End User License Agreement for use with Infineon
* integrated circuit products.
*******************************************************************************/

#ifndef RADAR_SETTINGS_H
#define RADAR_SETTINGS_H

#include <stdint.h>

#define XENSIV_BGT60TRXX_CONF_DEVICE                    (XENSIV_DEVICE_BGT60TR13C)
#define XENSIV_BGT60TRXX_CONF_START_FREQ_HZ             (58500000000ULL)
#define XENSIV_BGT60TRXX_CONF_END_FREQ_HZ               (62500000000ULL)
#define XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP     (64U)
#define XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME      (32U)
#define XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS           (3U)
#define XENSIV_BGT60TRXX_CONF_NUM_TX_ANTENNAS           (1U)
#define XENSIV_BGT60TRXX_CONF_SAMPLE_RATE               (2000000U)
#define XENSIV_BGT60TRXX_CONF_CHIRP_REPETITION_TIME_S   (0.000299787f)
#define XENSIV_BGT60TRXX_CONF_FRAME_REPETITION_TIME_S   (0.0300446f)
#define XENSIV_BGT60TRXX_CONF_NUM_REGS                  (38U)

#define XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_FRAME     (XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP * \
                                                         XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME * \
                                                         XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS)

#if defined(XENSIV_BGT60TRXX_CONF_IMPL)
static const uint32_t register_list_gesture[XENSIV_BGT60TRXX_CONF_NUM_REGS] =
{
    0x011e8270UL,
    0x030a0210UL,
    0x09e967fdUL,
    0x0b0805b4UL,
    0x0d102bffUL,
    0x0f010d00UL,
    0x11000000UL,
    0x13000000UL,
    0x15000000UL,
    0x17000be0UL,
    0x19000000UL,
    0x1b000000UL,
    0x1d000000UL,
    0x1f000b60UL,
    0x2113fc51UL,
    0x237ff41fUL,
    0x25000c63UL,
    0x2d000490UL,
    0x3b000480UL,
    0x49000480UL,
    0x57000480UL,
    0x5911be0eUL,
    0x5b56040aUL,
    0x5d01f000UL,
    0x5f787e1eUL,
    0x61b12902UL,
    0x6300091dUL,
    0x65000172UL,
    0x67000040UL,
    0x69000000UL,
    0x6b000000UL,
    0x6d000000UL,
    0x6f2a0b10UL,
    0x7f000100UL,
    0x8f000100UL,
    0x9f000100UL,
    0xad000000UL,
    0xb7000000UL
};
#endif /* XENSIV_BGT60TRXX_CONF_IMPL */

#endif /* RADAR_SETTINGS_H */

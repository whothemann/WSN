/**
 * @file external_sensors.h
 * @brief External sensor interfaces for button, buzzer, and heartbeat monitoring.
 *
 * This module provides interfaces for:
 * - Button input and buzzer output
 * - Heartbeat monitoring on BED_NODE devices
 *
 * The heartbeat value is normally derived from a real pulse sensor.
 * If no valid pulse signal is detected for a defined timeout period,
 * the module automatically falls back to generating plausible
 * (fake) BPM values.
 */

#ifndef EXTERNAL_SENSORS_H
#define EXTERNAL_SENSORS_H

#include <stdint.h>
#include "contiki.h"

/**
 * @brief Process for button polling, buzzer control, and pulse sampling.
 *
 * This process:
 * - Polls the emergency button
 * - Controls the buzzer
 * - Samples the pulse sensor (if available)
 *
 * It runs only on BED_NODE devices.
 */
PROCESS_NAME(button_buzzer_pulse_process);

/**
 * @brief Process for heartbeat monitoring and reporting.
 *
 * This process periodically sends heartbeat messages to the parent node.
 * It automatically decides whether to use:
 * - real BPM values from the pulse sensor, or
 * - generated (fake) BPM values if no real signal is available.
 */
PROCESS_NAME(heartbeat_monitor_process);

/**
 * @brief Get the current heartbeat rate in beats per minute (BPM).
 *
 * Returns the most recent BPM value.
 * - If a valid pulse signal is available, this is a real measured BPM.
 * - If no pulse signal is detected for some time, this may be a generated
 *   (fake) BPM value.
 *
 * @return Current BPM value, or 0 if no value is available yet.
 */
uint16_t get_current_bpm(void);

#endif /* EXTERNAL_SENSORS_H */

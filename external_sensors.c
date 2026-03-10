/**
 * @file external_sensors.c
 * @brief Implementation of external sensor interfaces for BED_NODE devices
 */

#include "contiki.h"
#include "nrf_gpio.h"
#include "common/saadc-sensor.h"
#include "msg_up.h"
#include "rpl_state.h"
#include "rpl_config.h"
#include "rpl_route.h"
#include "lib/random.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Pin configuration */

#define BUZZER_PIN   31
#define BUTTON1_PIN  11
#define PULSE_PIN    P0_30

/* -------------------------------------------------------------------------- */
/* Timing configuration */

#define BUTTON_POLL_INTERVAL        (CLOCK_SECOND / 50)
#define PULSE_SAMPLE_INTERVAL       (CLOCK_SECOND / 500)
#define HEARTBEAT_SEND_INTERVAL     (11 * CLOCK_SECOND)

/* Fake mode behavior */
#define FAKE_BPM_UPDATE_INTERVAL    (5 * CLOCK_SECOND)
/* If no real BPM update occurs within this timeout, switch to fake mode */
#define NO_REAL_BPM_TIMEOUT         (10 * CLOCK_SECOND)

/* -------------------------------------------------------------------------- */
/* Pulse filter configuration */

#define FILTER_SIZE     16
#define MIN_AMPLITUDE   30

/* -------------------------------------------------------------------------- */
/* Processes */

PROCESS(button_buzzer_pulse_process, "Button + Pulse Sensor");
PROCESS(heartbeat_monitor_process, "Heartbeat Monitor");

/* -------------------------------------------------------------------------- */
/* Module state */

static uint16_t raw;
static int16_t filtered;
static int16_t peak = 0, trough = 0;
static int16_t threshold = 0;
static bool pulse = false;

static clock_time_t last_beat = 0;
static uint16_t bpm = 0;

/* Fake/real control */
static uint8_t fake_mode = 0;                 /* 0 = real, 1 = fake */
static clock_time_t last_real_bpm_time = 0;   /* last time we updated bpm from real sensor */
static clock_time_t last_fake_update_time = 0;
static uint16_t fake_bpm = 0;

/* -------------------------------------------------------------------------- */
/**
 * @brief Get current heartbeat rate.
 * @return Current BPM value (real or fake), or 0 if unknown.
 */
uint16_t get_current_bpm(void)
{
  if(fake_mode) {
    return fake_bpm;
  }
  return bpm;
}

/* -------------------------------------------------------------------------- */
/* DC filter implementation */

static uint16_t buf[FILTER_SIZE];
static uint32_t sum = 0;
static uint8_t idx = 0;

/**
 * @brief Apply DC (high-pass) filter to remove baseline drift.
 * @param v Raw ADC value to filter.
 * @return DC-filtered value.
 */
static int16_t dc_filter(uint16_t v)
{
  sum -= buf[idx];
  buf[idx] = v;
  sum += v;
  idx = (idx + 1) % FILTER_SIZE;
  return (int16_t)(v - (sum / FILTER_SIZE));
}

/* -------------------------------------------------------------------------- */
/**
 * @brief Generate a plausible fake BPM value in the range [50..150].
 * @return Fake BPM.
 */
static uint16_t generate_fake_bpm(void)
{
  /* random_rand() is provided by Contiki; range is platform dependent.
     We map it to 50..150 inclusive (101 values). */
  return (uint16_t)(50 + (random_rand() % 101));
}

/* -------------------------------------------------------------------------- */
/**
 * @brief Process pulse signal to detect heartbeat.
 * Uses peak/trough detection with an adaptive threshold to compute BPM.
 * If a valid beat interval is detected, it updates bpm and disables fake_mode.
 *
 * @param v   Raw ADC reading.
 * @param now Current clock time.
 */
static void process_pulse(uint16_t v, clock_time_t now)
{
  filtered = dc_filter(v);

  if(filtered > peak) peak = filtered;
  if(filtered < trough) trough = filtered;

  if(filtered > threshold && !pulse) {
    pulse = true;

    clock_time_t ibi = now - last_beat;
    last_beat = now;

    if(ibi > CLOCK_SECOND / 4 && ibi < CLOCK_SECOND * 2) {
      bpm = (uint16_t)((60UL * CLOCK_SECOND) / (unsigned long)ibi);

      /* Mark as real BPM */
      last_real_bpm_time = now;
      fake_mode = 0;
    }
  }

  if(filtered < threshold && pulse) {
    pulse = false;

    if((peak - trough) > MIN_AMPLITUDE) {
      threshold = trough + (peak - trough) / 2;
    }

    peak = 0;
    trough = 0;
  }
}

/* -------------------------------------------------------------------------- */
/**
 * @brief Button + pulse sampling process.
 * Runs only on ROLE_NODE devices.
 */
PROCESS_THREAD(button_buzzer_pulse_process, ev, data)
{
  static struct etimer button_timer, pulse_timer;
  static uint8_t last_button_state = 1;

  PROCESS_BEGIN();

  if(MY_ROLE != ROLE_NODE) {
    PROCESS_EXIT();
  }

  nrf_gpio_cfg_output(BUZZER_PIN);
  nrf_gpio_pin_clear(BUZZER_PIN);
  nrf_gpio_cfg_input(BUTTON1_PIN, NRF_GPIO_PIN_PULLUP);

  SENSORS_ACTIVATE(saadc_sensor);

  etimer_set(&button_timer, BUTTON_POLL_INTERVAL);
  etimer_set(&pulse_timer, PULSE_SAMPLE_INTERVAL);

  while(1) {
    PROCESS_WAIT_EVENT();

    /* Pulse sampling */
    if(ev == PROCESS_EVENT_TIMER && data == &pulse_timer) {
      raw = (uint16_t)saadc_sensor.value(PULSE_PIN);
      process_pulse(raw, clock_time());
      etimer_reset(&pulse_timer);
    }

    /* Button handling */
    if(ev == PROCESS_EVENT_TIMER && data == &button_timer) {
      uint8_t current_state = nrf_gpio_pin_read(BUTTON1_PIN);

      if(current_state == 0 && last_button_state == 1) {
        nrf_gpio_pin_set(BUZZER_PIN);
        etimer_set(&button_timer, CLOCK_SECOND);
      } else if(current_state == 1 && last_button_state == 0) {
        nrf_gpio_pin_clear(BUZZER_PIN);
      }

      last_button_state = current_state;
      etimer_reset(&button_timer);
    }
  }

  PROCESS_END();
}

/* -------------------------------------------------------------------------- */
/**
 * @brief Heartbeat monitoring and transmission process.
 *
 * - Sends heartbeat every HEARTBEAT_SEND_INTERVAL.
 * - If no real BPM update happened within NO_REAL_BPM_TIMEOUT,
 *   switches to fake mode.
 * - In fake mode, a new random BPM is generated every FAKE_BPM_UPDATE_INTERVAL.
 */
PROCESS_THREAD(heartbeat_monitor_process, ev, data)
{
  static struct etimer tick;
  static clock_time_t last_send_time = 0;

  PROCESS_BEGIN();

  if(MY_ROLE != ROLE_NODE) {
    PROCESS_EXIT();
  }

  /* Seed-ish: combine time and link-layer last byte (if available) */
  random_init((unsigned short)(clock_time() ^ (unsigned short)linkaddr_node_addr.u8[LINKADDR_SIZE - 1]));

  /* Start in "unknown" state: allow real pulse to lock in, else fallback */
  fake_mode = 0;
  last_real_bpm_time = clock_time();
  last_fake_update_time = 0;
  fake_bpm = 0;
  last_send_time = 0;

  /* 1-second tick simplifies scheduling of both 3s sends and 5s fake updates */
  etimer_set(&tick, CLOCK_SECOND);

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&tick));

    clock_time_t now = clock_time();

    /* Decide fake mode based on real BPM freshness */
    if((now - last_real_bpm_time) > NO_REAL_BPM_TIMEOUT) {
      if(!fake_mode) {
        fake_mode = 1;
        last_fake_update_time = 0; /* force immediate generation */
      }
    }

    /* Update fake value every 5 seconds when in fake mode */
    if(fake_mode) {
      if(last_fake_update_time == 0 || (now - last_fake_update_time) >= FAKE_BPM_UPDATE_INTERVAL) {
        fake_bpm = generate_fake_bpm();
        last_fake_update_time = now;
      }
    }

    /* Send heartbeat every HEARTBEAT_SEND_INTERVAL */
    if(last_send_time == 0 || (now - last_send_time) >= HEARTBEAT_SEND_INTERVAL) {
      uint16_t to_send = fake_mode ? fake_bpm : bpm;

      /* In real mode, only send if we have a plausible bpm */
      if(fake_mode || to_send > 0) {
        msg_up_send_heartbeat(to_send);
      }

      last_send_time = now;
    }

    etimer_reset(&tick);
  }

  PROCESS_END();
}

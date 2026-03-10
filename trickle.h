/**
 * @file trickle.h
 * @brief Trickle timer implementation for RPL DIO messages
 *
 * Implements a simplified trickle timer for the root and access points
 * to control DIO message broadcast frequency. Starts with short intervals
 * and backs off exponentially to conserve energy.
 */

#ifndef TRICKLE_H
#define TRICKLE_H

#include "contiki.h"
#include "sys/clock.h"
#include "sys/etimer.h"
#include "net/linkaddr.h"

#include <stdint.h>

/**
 * @brief Trickle timer state structure
 *
 * Manages the exponential backoff interval timing used by the root/AP
 * to control DIO broadcast frequency. No redundancy counting (k) or
 * consistency checking (c) as simplified for this RPL variant.
 */
typedef struct
{
  clock_time_t Imin;
  clock_time_t Imax;
  clock_time_t I;

  struct etimer interval_timer;
  struct etimer tx_timer;
  uint8_t tx_armed;

  clock_time_t last_reset;
} trickle_t;

/**
 * @brief Initialize a trickle timer
 *
 * Sets up the timer with specified minimum interval and doubling count.
 * Immediately starts the first interval.
 *
 * @param tt         Pointer to trickle timer structure
 * @param Imin       Minimum interval (clock ticks)
 * @param doublings  Number of times to double Imin to reach Imax
 */
void trickle_init(trickle_t *tt, clock_time_t Imin, uint8_t doublings);

/**
 * @brief Reset trickle timer to minimum interval
 *
 * Forces the interval back to Imin and increments the DODAG version.
 * Called when topology change is detected or DIS is received.
 * Restarts a new interval immediately.
 *
 * @param tt  Pointer to trickle timer structure
 */
void trickle_reset(trickle_t *tt);

/**
 * @brief Handle end of current interval
 *
 * Called when the interval timer expires. Doubles the interval
 * (up to Imax) and starts a new interval.
 *
 * @param tt  Pointer to trickle timer structure
 */
void trickle_on_interval_expired(trickle_t *tt);

/**
 * @brief Check if it's time to transmit
 *
 * @param tt  Pointer to trickle timer structure
 * @return    1 if tx_timer has expired and is armed, 0 otherwise
 */
uint8_t trickle_tx_ready(trickle_t *tt);

/**
 * @brief Check if interval has expired
 *
 * @param tt  Pointer to trickle timer structure
 * @return    1 if interval_timer has expired, 0 otherwise
 */
uint8_t trickle_interval_ready(trickle_t *tt);

/**
 * @brief Mark transmission as sent
 *
 * Call after handling tx_ready() to disarm the tx timer
 * and prevent multiple transmissions in one interval.
 *
 * @param tt  Pointer to trickle timer structure
 */
void trickle_mark_tx_sent(trickle_t *tt);

/**
 * @brief Get transmit timer
 *
 * @param tt  Pointer to trickle timer structure
 * @return    Pointer to the tx etimer
 */
struct etimer *trickle_tx_etimer(trickle_t *tt);

/**
 * @brief Get interval timer
 *
 * @param tt  Pointer to trickle timer structure
 * @return    Pointer to the interval etimer
 */
struct etimer *trickle_interval_etimer(trickle_t *tt);

/**
 * @brief Handle received trickle message
 *
 * Forwards trickle messages up the tree toward the root.
 * Used by access points to notify the root of topology changes.
 *
 * @param buf       Message buffer
 * @param len       Message length
 * @param src       Sender's link-layer address
 * @return          1 if handled, 0 otherwise
 */
int msg_trickle_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src);

#endif /* TRICKLE_H */
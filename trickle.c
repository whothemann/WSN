/**
 * @file trickle.c
 * @brief Implementation of trickle timer for DIO message scheduling
 */

#include "contiki.h"
#include "trickle.h"
#include "lib/random.h"
#include "rpl_config.h"
#include "rpl_state.h"
#include "rpl_route.h"
#include "net/linkaddr.h"
#include "rpl_net.h"

/**
 * @brief Generate random number in range [lo, hi_exclusive)
 *
 * @param lo                Lower bound (inclusive)
 * @param hi_exclusive      Upper bound (exclusive)
 * @return                  Random value in the range
 */
static clock_time_t
rand_in_range(clock_time_t lo, clock_time_t hi_exclusive)
{
    uint16_t span = (uint16_t)(hi_exclusive - lo);
    if (span == 0)
    {
        return lo;
    }
    return lo + (clock_time_t)(random_rand() % span);
}

/**
 * @brief Start a new trickle interval
 *
 * Sets up the interval timer and chooses a random transmission
 * time within [I/2, I).
 *
 * @param tt  Pointer to trickle timer
 */
static void
start_interval(trickle_t *tt)
{
    etimer_set(&tt->interval_timer, tt->I);

    clock_time_t half = tt->I / 2;
    clock_time_t t = rand_in_range(half, tt->I);

    etimer_set(&tt->tx_timer, t);
    tt->tx_armed = 1;
}

/**
 * @brief Initialize trickle timer
 *
 * Sets minimum interval, maximum interval, and starts the first interval.
 *
 * @param tt        Pointer to trickle timer
 * @param Imin      Minimum interval
 * @param doublings Number of times to double Imin to reach Imax
 */
void trickle_init(trickle_t *tt, clock_time_t Imin, uint8_t doublings)
{
    tt->Imin = Imin;
    tt->Imax = (clock_time_t)(Imin << doublings);
    tt->I = tt->Imin;
    tt->tx_armed = 0;

    tt->last_reset = 0;

    start_interval(tt);
}
    
/**
 * @brief Reset trickle timer and increment DODAG version
 *
 * Resets interval to Imin and increments the DODAG version number.
 * Called when topology changes or DIS is received.
 *
 * @param tt  Pointer to trickle timer
 */
void trickle_reset(trickle_t *tt)
{
    tt->I = tt->Imin;

    etimer_stop(&tt->interval_timer);
    etimer_stop(&tt->tx_timer);
    tt->tx_armed = 0;

    clock_time_t now = clock_time();
    if (tt->last_reset == 0 ||
        (now - tt->last_reset) >= CLOCK_SECOND)
    {
        tt->last_reset = now;
    }

    start_interval(tt);
}

/**
 * @brief Handle interval expiration
 *
 * Doubles the interval (capped at Imax) and starts a new interval.
 *
 * @param tt  Pointer to trickle timer
 */
void trickle_on_interval_expired(trickle_t *tt)
{
    if (tt->I < tt->Imax)
    {
        tt->I = tt->I * 2;
        if (tt->I > tt->Imax)
        {
            tt->I = tt->Imax;
        }
    }

    start_interval(tt);
}

/**
 * @brief Check if transmission time has arrived
 *
 * @param tt  Pointer to trickle timer
 * @return    1 if tx timer expired and is armed, 0 otherwise
 */
uint8_t
trickle_tx_ready(trickle_t *tt)
{
    return tt->tx_armed && etimer_expired(&tt->tx_timer);
}

/**
 * @brief Check if interval has expired
 *
 * @param tt  Pointer to trickle timer
 * @return    1 if interval timer expired, 0 otherwise
 */
uint8_t
trickle_interval_ready(trickle_t *tt)
{
    return etimer_expired(&tt->interval_timer);
}

/**
 * @brief Mark that transmission has been sent
 *
 * Disarm the tx timer after a transmission to prevent
 * multiple sends in the same interval.
 *
 * @param tt  Pointer to trickle timer
 */
void trickle_mark_tx_sent(trickle_t *tt)
{
    tt->tx_armed = 0;
}

/**
 * @brief Get the transmit etimer
 *
 * @param tt  Pointer to trickle timer
 * @return    Pointer to tx etimer
 */
struct etimer *
trickle_tx_etimer(trickle_t *tt)
{
    return &tt->tx_timer;
}

/**
 * @brief Get the interval etimer
 *
 * @param tt  Pointer to trickle timer
 * @return    Pointer to interval etimer
 */
struct etimer *
trickle_interval_etimer(trickle_t *tt)
{
    return &tt->interval_timer;
}

/**
 * @brief Handle received trickle message
 *
 * Forwards trickle messages from access points toward the root.
 * Used to notify the root of topology changes.
 *
 * @param buf   Message buffer
 * @param len   Message length
 * @param src   Sender's link-layer address
 * @return      1 if forwarded, 0 otherwise
 */
int msg_trickle_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src)
{
    if (buf == NULL || len == 0 || src == NULL)
    {
        return 0;
    }

    const linkaddr_t *my_parent = rpl_state_get_parent_lladdr();
    if (my_parent->u8[LINKADDR_SIZE - 1] != 0)
    {
        rpl_unicast_addr(buf, len, my_parent);
        return 1;
    }
    return 0;
}
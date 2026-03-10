/**
 * @file msg_down.c
 * @brief Implementation of downward message routing from root to nodes
 *
 * This module handles:
 * - Sending MSG_DOWN control messages from root to target devices (typically nurses)
 * - Building and maintaining paths through the RPL tree to reach destination nodes
 * - Reliable delivery with acknowledgments and retry logic
 * - Forwarding messages along the path to destination
 * - Nurse-side device control (LED indicators, buzzer feedback)
 * - Message acknowledgment propagation back to the root
 *
 * **Message Flow:**
 * 1. Root determines target device by role
 * 2. Root builds path from itself to target via routing table
 * 3. Root sends MSG_DOWN with path information
 * 4. Intermediate nodes forward to next hop in path
 * 5. Target node receives and applies changes (LEDs, buzzer)
 * 6. Target sends MSG_DOWN_ACK back via parent to root
 *
 * @author WSN Lab Group 6
 * @date 2026
 *
 * @defgroup msg_down Downward Message Routing
 * @{
 */

#include "msg_down.h"

#include "rpl_net.h"
#include "rpl_state.h"
#include "rpl_route.h"
#include "rpl_config.h"
#include "rpl_msg.h"
#include "dev/leds.h"
#include "nrf_gpio.h"

#include "contiki.h"
#include "sys/process.h"
#include "sys/etimer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * @defgroup msg_down_config Configuration
 * @{
 */
#define MSG_DOWN_MAX_LEN 256                                       /**< Maximum message length in bytes */
#define MSG_DOWN_MAX_HOPS 16                                       /**< Maximum number of hops in path */
#define MSG_DOWN_MAX_RETRIES 20                                     /**< Maximum retry attempts for delivery */
#define BUZZER_PIN 31                                              /**< GPIO pin number for buzzer control */
#define BUZZER_BEEP_MS CLOCK_SECOND / 10                           /**< Buzzer pulse duration in milliseconds */
#define BUZZER_BEEP_TICKS ((BUZZER_BEEP_MS * CLOCK_SECOND) / 1000) /**< On-duration ticks */
/** @} */

/**
 * @brief Downward message delivery process
 *
 * Manages retransmission of MSG_DOWN messages and ACK timeout handling.
 * Ensures delivery of control messages from root to target nodes.
 */
PROCESS(msg_down_reliable_process, "MSG_DOWN reliable");
static uint8_t reliable_process_started; /**< Flag: process has been started */

/**
 * @defgroup msg_down_state Root Pending Message State
 * @brief State tracking for reliable delivery at the root
 * @{
 */
static uint8_t pending_active;            /**< Message is pending delivery */
static char pending_label[32];            /**< Target device label */
static uint16_t pending_seq;              /**< Message sequence number */
static uint8_t pending_retries_left;      /**< Remaining retry attempts */
static uint8_t pending_waiting_for_route; /**< Route not yet available */
static uint8_t pending_waiting_for_ack;   /**< Waiting for target ACK */
static uint16_t seq_counter;              /**< Global sequence counter */
static int pending_heart_emergency;       /**< Is this a heartbeat emergency */
/** @} */

/**
 * @brief Ensure the reliable delivery process is running
 *
 * The process is started on demand so that nurse nodes (which do not call
 * msg_down_send_to_nurse) still run the timer loop needed for the buzzer.
 */
static void ensure_reliable_process_started(void)
{
  if (!reliable_process_started)
  {
    process_start(&msg_down_reliable_process, NULL);
    reliable_process_started = 1;
  }
}

/**
 * @defgroup msg_down_helpers Helper Functions
 * @brief Utility functions for message parsing and address manipulation
 * @{
 */

/**
 * @brief Convert a link-layer address to hexadecimal string representation
 *
 * Converts a binary link address to a hex string (e.g., "1122").
 *
 * @param out Output buffer for the string
 * @param out_len Size of the output buffer
 * @param a Pointer to the link address to convert
 */
static void lladdr_to_str(char *out, size_t out_len, const linkaddr_t *a)
{
  if (!out || !a || out_len == 0)
    return;

  out[0] = '\0';
  size_t used = 0;

  for (size_t i = 0; i < LINKADDR_SIZE; i++)
  {
    if (used + 2 >= out_len)
      break;
    int n = snprintf(out + used, out_len - used, "%02x", a->u8[i]);
    if (n < 0)
    {
      out[0] = '\0';
      return;
    }
    used += (size_t)n;
  }
}

/**
 * @brief Parse a hexadecimal byte from a string
 *
 * @param p Pointer to the start of the hex string (must be at least 2 characters)
 * @param out Output buffer for the parsed byte value
 * @return 1 if parsing was successful, 0 otherwise
 */
static int parse_hex_byte(const char *p, uint8_t *out)
{
  char tmp[3] = {p[0], p[1], '\0'};
  char *end = NULL;
  long v = strtol(tmp, &end, 16);
  if (end == tmp || v < 0 || v > 255)
    return 0;
  *out = (uint8_t)v;
  return 1;
}

/**
 * @brief Parse a link-layer address from hexadecimal string representation
 *
 * @param s Hexadecimal string representation of the address
 * @param out Output buffer for the parsed address
 * @return 1 if parsing was successful, 0 otherwise
 */
static int parse_lladdr(const char *s, linkaddr_t *out)
{
  if (!s || !out)
    return 0;
  if (strlen(s) != LINKADDR_SIZE * 2)
    return 0;

  for (size_t i = 0; i < LINKADDR_SIZE; i++)
  {
    if (!parse_hex_byte(&s[i * 2], &out->u8[i]))
      return 0;
  }
  return 1;
}

/**
 * @brief Find and return the value of a field in a message string
 *
 * Message format: "FIELD1:value1 FIELD2:value2 ..."
 *
 * @param msg The message string to search
 * @param key The field key to find
 * @return Pointer to the field value, or NULL if not found
 */
static const char *find_field(const char *msg, const char *key)
{
  size_t klen = strlen(key);
  const char *p = msg;

  while ((p = strstr(p, key)) != NULL)
  {
    if ((p == msg || p[-1] == ' ') && p[klen] == ':')
      return p + klen + 1;
    p += klen;
  }
  return NULL;
}

/**
 * @brief Copy a field value from the message (up to space or end of string)
 *
 * @param dst Destination buffer
 * @param dst_len Size of destination buffer
 * @param val Source value pointer
 * @return 1 if at least one character was copied, 0 otherwise
 */
static int copy_field_value(char *dst, size_t dst_len, const char *val)
{
  size_t i = 0;
  while (val[i] && val[i] != ' ' && i + 1 < dst_len)
  {
    dst[i] = val[i];
    i++;
  }
  dst[i] = '\0';
  return i > 0;
}

/** @} */

/**
 * @defgroup msg_down_nurse Nurse Device Control
 * @brief Functions for controlling nurse station LEDs and buzzer
 * @{
 */

/**
 * @brief Extract bed number from a device label string
 *
 * Parses labels in the format "BED<N>" to extract the bed number.
 * Handles special "EMPTY" label indicating no active emergency.
 *
 * @param label Device label string (e.g., "BED1", "BED2", "EMPTY")
 * @return Bed number (1-255), or 0 if label is invalid or "EMPTY"
 */
static uint8_t bed_no_from_label(const char *label)
{
  if (!label || strcmp(label, "EMPTY") == 0)
    return 0;
  if (strncmp(label, "BED", 3) != 0)
    return 0;

  char *end = NULL;
  long n = strtol(label + 3, &end, 10);
  if (end == label + 3 || n <= 0 || n > 255)
    return 0;
  return (uint8_t)n;
}

/**
 * @defgroup msg_down_buzzer_state Nurse Buzzer State
 * @brief State tracking for buzzer control at nurse station
 * @{
 */
static uint8_t buzzer_persistent_active = 0; /**< Persistent buzzer is active */
static struct etimer buzzer_timer;            /**< Timer for persistent buzzer pulses */
static uint8_t buzzer_on_phase = 0;           /**< 1 while buzzer is driven high */
/** @} */

/**
 * @brief Emit a short buzzer beep
 *
 * Activates the buzzer for a short pulse duration to provide audio feedback.
 * Uses the configured BUZZER_PIN and BUZZER_BEEP_MS duration.
 */
static void buzzer_beep_short(void)
{
  nrf_gpio_pin_set(BUZZER_PIN);
  clock_wait(CLOCK_SECOND / 10);

  nrf_gpio_pin_clear(BUZZER_PIN);
}

/**
 * @brief Start continuous buzzer pulses for heartbeat emergencies
 *
 * Activates persistent buzzer feedback with periodic pulses to indicate
 * a critical heartbeat emergency situation.
 */
static void buzzer_start_persistent(void)
{
  buzzer_persistent_active = 1;
  buzzer_on_phase = 1;
  nrf_gpio_pin_set(BUZZER_PIN);
  etimer_set(&buzzer_timer, BUZZER_BEEP_TICKS ? BUZZER_BEEP_TICKS : 1);
}

/**
 * @brief Stop continuous buzzer and return to normal mode
 */
static void buzzer_stop_persistent(void)
{
  buzzer_persistent_active = 0;
  buzzer_on_phase = 0;
  etimer_stop(&buzzer_timer);
  nrf_gpio_pin_clear(BUZZER_PIN);
}

/**
 * @brief Apply label information to nurse station (LEDs and buzzer)
 *
 * Updates the LED display to indicate which bed has an emergency and
 * provides audio feedback when a new emergency is detected.
 *
 * **LED Mapping:**
 * - LED1: Bed 1
 * - LED2: Bed 2
 * - LED3: Bed 3
 * - LED4: Bed 4
 * - All LEDs off: No emergency ("EMPTY" or invalid label)
 *
 * **Buzzer Behavior:**
 * - Heartbeat emergency: Continuous pulsing buzzer (persistent warning)
 * - Regular emergency: Single short beep on state change
 * - No emergency: Buzzer off
 *
 * @param label              Device label indicating active emergency (e.g., "BED1", "EMPTY")
 * @param heart_emergency    Non-zero if this is a heartbeat emergency (triggers persistent buzzer)
 *
 * @note Only beeps when transitioning to a different label (not on repeated calls)
 *       Heartbeat emergencies trigger continuous buzzer until cleared
 */
static void nurse_apply_label(const char *label, int heart_emergency)
{
  static char last_label[32] = "";
  static uint8_t buzzer_inited = 0;

  if (!buzzer_inited)
  {
    nrf_gpio_cfg_output(BUZZER_PIN);
    nrf_gpio_pin_clear(BUZZER_PIN);
    buzzer_inited = 1;
  }

  leds_off(LEDS_ALL);

  if (!label || strcmp(label, "EMPTY") == 0)
  {
    last_label[0] = '\0';
    buzzer_stop_persistent();
    return;
  }

  uint8_t bed = bed_no_from_label(label);
  if (bed == 0)
  {
    buzzer_stop_persistent();
    return;
  }

  if (strcmp(last_label, label) != 0)
  {
    strncpy(last_label, label, sizeof(last_label) - 1);
    last_label[sizeof(last_label) - 1] = '\0';
    
    /* Short beep for regular emergencies, persistent for heartbeat */
    if (!heart_emergency)
    {
      buzzer_beep_short();
      buzzer_stop_persistent();
    }
    else
    {
      buzzer_start_persistent();
    }
  }
  else if (heart_emergency && !buzzer_persistent_active)
  {
    /* Same label but now with heartbeat emergency -> activate persistent */
    buzzer_start_persistent();
  }
  else if (!heart_emergency && buzzer_persistent_active)
  {
    /* Was persistent, now regular -> switch off persistent */
    buzzer_stop_persistent();
  }

  switch (bed)
  {
  case 1:
    leds_single_on(LEDS_LED1);
    break;
  case 2:
    leds_single_on(LEDS_LED2);
    break;
  case 3:
    leds_single_on(LEDS_LED3);
    break;
  case 4:
    leds_single_on(LEDS_LED4);
    break;
  default:
    leds_off(LEDS_ALL);
    break;
  }
}

/** @} */

/**
 * @defgroup msg_down_root Root Message Transmission
 * @brief Functions for sending MSG_DOWN messages from the root
 * @{
 */

/**
 * @brief Attempt to send the pending MSG_DOWN message
 *
 * Looks up the target nurse by role, builds the path from root to nurse,
 * and sends the MSG_DOWN message with the path information. If the route
 * is not available, marks pending_waiting_for_route to retry later.
 *
 * **Message Format:**
 * ```
 * MSG_DOWN seq:<seq> label:<target> hops:<hop1>|<hop2>|...|<hopN>
 * ```
 *
 * The receiving node will forward to the next hop in the path, or apply
 * the label if it is the final destination.
 *
 * @note Sets pending_waiting_for_ack flag when message is sent successfully
 * @note Sets pending_waiting_for_route flag if route is not available
 *
 * @see rpl_route_get_addr_by_role() to find target device
 * @see rpl_route_build_path() to construct path
 */
static void root_try_send_now(void)
{
  linkaddr_t nurse;
  linkaddr_t hops[MSG_DOWN_MAX_HOPS];
  uint8_t hop_count = 0;

  /* Find nurse by role in routing table */
  if (!rpl_route_get_addr_by_role(ROLE_NURSE, &nurse))
  {
    pending_waiting_for_route = 1;
    return;
  }

  /* Build path from root to nurse */
  if (!rpl_route_build_path(&nurse, hops, MSG_DOWN_MAX_HOPS, &hop_count) || hop_count == 0)
  {
    pending_waiting_for_route = 1;
    return;
  }

  /* Format hops as pipe-separated list */
  char hops_str[MSG_DOWN_MAX_LEN];
  hops_str[0] = '\0';

  for (uint8_t i = 0; i < hop_count; i++)
  {
    char a_str[3 * LINKADDR_SIZE + 1];
    lladdr_to_str(a_str, sizeof(a_str), &hops[i]);

    size_t used = strlen(hops_str);
    size_t need = strlen(a_str) + (i ? 1 : 0);
    if (used + need + 1 >= sizeof(hops_str))
      break;

    if (i)
      strncat(hops_str, "|", sizeof(hops_str) - used - 1);
    strncat(hops_str, a_str, sizeof(hops_str) - strlen(hops_str) - 1);
  }

  /* Format and send MSG_DOWN message with heartbeat emergency flag */
  char msg[MSG_DOWN_MAX_LEN];
  int n = snprintf(msg, sizeof(msg),
                   "MSG_DOWN seq:%u label:%.31s heartbeat_emerg:%d hops:%s",
                   (unsigned)pending_seq, pending_label, (int)pending_heart_emergency, hops_str);

  if (n < 0 || (size_t)n >= sizeof(msg))
  {
    pending_waiting_for_route = 1;
    return;
  }

  rpl_unicast_addr(msg, (uint16_t)strlen(msg), &hops[0]);

  pending_waiting_for_route = 0;
  pending_waiting_for_ack = 1;
}

/**
 * @brief Send a downward message to the nurse station
 *
 * Initiates a new MSG_DOWN delivery to communicate which emergency needs
 * attention. The message will be delivered to the nurse via the RPL tree.
 * The function starts the reliable delivery process with retries.
 *
 * **Typical Usage:**
 * - Root calls this when emergency table priority changes
 * - Message identifies which bed/node needs attention
 * - Label "EMPTY" clears all emergencies
 *
 * @param label Target device label (e.g., "BED1", "EMPTY")
 *
 * @note This is typically called from msg_up_handle_rx() when emergency
 *       table priority changes.
 *
 * @see msg_up_handle_rx()
 */
void msg_down_send_to_nurse(const char *label, int heart_emergency)
{
  if (!label)
    return;

  if (!reliable_process_started)
  {
    process_start(&msg_down_reliable_process, NULL);
    reliable_process_started = 1;
  }

  strncpy(pending_label, label, sizeof(pending_label) - 1);
  pending_label[sizeof(pending_label) - 1] = '\0';

  pending_active = 1;
  pending_waiting_for_route = 0;
  pending_waiting_for_ack = 0;
  pending_seq = ++seq_counter;
  pending_retries_left = MSG_DOWN_MAX_RETRIES;
  pending_heart_emergency = heart_emergency;

  root_try_send_now();
  process_poll(&msg_down_reliable_process);
}

/** @} */

/**
 * @defgroup msg_down_rx Reception and Forwarding
 * @brief Functions for receiving and processing MSG_DOWN messages
 * @{
 */

/**
 * @brief Handle received MSG_DOWN or MSG_DOWN_ACK messages
 *
 * This function processes both downward messages and acknowledgments:
 *
 * **MSG_DOWN_ACK Handling:**
 * - At root: Marks message as successfully delivered
 * - At intermediate nodes: Forwards ACK to parent toward root
 * - Contains sequence number to match with pending message
 *
 * **MSG_DOWN Handling:**
 * - Parses label and hop path from message
 * - If this is the final destination: applies label to nurse device
 * - If not final: forwards to next hop in path
 * - Sends ACK back via parent
 *
 * **Path-Based Forwarding:**
 * The hops field contains a pipe-separated list of addresses.
 * Each node:
 * 1. Extracts the first hop (current node)
 * 2. Takes remaining hops as the next path
 * 3. Sends to the next hop address
 *
 * @param buf The received message buffer
 * @param len Length of the received message
 * @param src Source link-layer address (unused)
 * @return 1 if message was handled, 0 otherwise
 *
 * @see nurse_apply_label() for destination processing
 */
int msg_down_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src)
{
  (void)src;

  /* Ensure reliable process is active so buzzer timer events are handled */
  ensure_reliable_process_started();

  char tmp[MSG_DOWN_MAX_LEN];
  len = (len < sizeof(tmp) - 1) ? len : sizeof(tmp) - 1;
  memcpy(tmp, buf, len);
  tmp[len] = '\0';

  /* ================================================================ */
  /* Handle MSG_DOWN_ACK (forward to parent or mark delivery done) */
  /* ================================================================ */
  if (strncmp(tmp, "MSG_DOWN_ACK", 12) == 0)
  {
    const linkaddr_t *p = rpl_state_get_parent_lladdr();
    /* If root, mark delivery complete */
    if (p && p->u8[LINKADDR_SIZE - 1] == 0)
    {
      pending_active = 0;
      pending_waiting_for_ack = 0;
    }
    /* Otherwise forward ACK to parent */
    else if (p)
    {
      rpl_unicast_addr(tmp, len, p);
    }
    return 1;
  }

  /* ================================================================ */
  /* Handle MSG_DOWN (forward or apply) */
  /* ================================================================ */
  if (strncmp(tmp, "MSG_DOWN", 8) != 0)
    return 0;

  char label[32] = {0};
  char hops[MSG_DOWN_MAX_LEN] = {0};
  int heartbeat_emerg = 0;

  /* Parse label, heartbeat_emerg, and hops fields */
  const char *l = find_field(tmp, "label");
  const char *h = find_field(tmp, "hops");
  const char *he = find_field(tmp, "heartbeat_emerg");
  if (!l || !h)
    return 1;

  copy_field_value(label, sizeof(label), l);
  copy_field_value(hops, sizeof(hops), h);
  if (he)
  {
    heartbeat_emerg = (find_field(tmp, "heartbeat_emerg")[0] != '0');
  }

  /* Parse hops: first token is current node, next is target */
  char *tok = strtok(hops, "|");
  if (!tok)
    return 1;

  char *next = strtok(NULL, "|");
  if (!next)
  {
    /* No next hop: this is the destination node */
    nurse_apply_label(label, heartbeat_emerg);

    /* Send ACK back to parent */
    char ack[64];
    snprintf(ack, sizeof(ack), "MSG_DOWN_ACK seq:%u label:%s",
             (unsigned)pending_seq, label);

    const linkaddr_t *p = rpl_state_get_parent_lladdr();
    if (p)
      rpl_unicast_addr(ack, strlen(ack), p);
    return 1;
  }

  /* Parse next hop address and forward message */
  linkaddr_t nh;
  if (!parse_lladdr(next, &nh))
    return 1;

  char fwd[MSG_DOWN_MAX_LEN];
  int fn = snprintf(fwd, sizeof(fwd),
                    "MSG_DOWN seq:%u label:%.31s heartbeat_emerg:%d hops:%s",
                    (unsigned)pending_seq, label, heartbeat_emerg, next);

  if (fn < 0 || (size_t)fn >= sizeof(fwd))
    return 1;

  /* Forward to next hop in path */
  rpl_unicast_addr(fwd, strlen(fwd), &nh);
  return 1;
}

/** @} */

/**
 * @defgroup msg_down_process Reliable Delivery Process
 * @{
 */

/**
 * @brief Reliable delivery process thread
 *
 * Manages the timeout and retry logic for MSG_DOWN message delivery.
 * Waits for acknowledgments from the destination and retransmits if
 * ACK is not received within MSG_DOWN_ACK_TIMEOUT.
 *
 * **State Machine:**
 * - Waits for events (timers or process polls)
 * - On ACK timeout: if retries remain, calls root_try_send_now() to retransmit
 * - If max retries exceeded: marks delivery as complete (gives up)
 * - Timer active only while message is pending
 *
 * @see root_try_send_now()
 * @see msg_down_send_to_nurse()
 */
PROCESS_THREAD(msg_down_reliable_process, ev, data)
{
  static struct etimer t;

  PROCESS_BEGIN();

  while (1)
  {
    PROCESS_WAIT_EVENT();

    /* Handle persistent buzzer pulses for heartbeat emergencies */
    if (ev == PROCESS_EVENT_TIMER && data == &buzzer_timer && buzzer_persistent_active)
    {
      if (buzzer_on_phase)
      {
        /* End of ON phase -> go low and wait for next pulse window */
        nrf_gpio_pin_clear(BUZZER_PIN);
        buzzer_on_phase = 0;
        etimer_set(&buzzer_timer, CLOCK_SECOND / 2); /* off interval */
      }
      else
      {
        /* Begin ON phase */
        nrf_gpio_pin_set(BUZZER_PIN);
        buzzer_on_phase = 1;
        etimer_set(&buzzer_timer, BUZZER_BEEP_TICKS ? BUZZER_BEEP_TICKS : 1);
      }
      continue;
    }

    /* Handle ACK timeout */
    if (ev == PROCESS_EVENT_TIMER && data == &t && pending_active)
    {
      if (pending_retries_left-- > 0)
      {
        /* Retry: send again */
        root_try_send_now();
      }
      else
      {
        /* Max retries exceeded */
        pending_active = 0;
      }
    }

    /* Update timer based on pending state */
    if (pending_active)
    {
      etimer_set(&t, MSG_DOWN_ACK_TIMEOUT);
    }
    else
    {
      etimer_stop(&t);
    }
  }

  PROCESS_END();
}

/** @} */
/** @} */

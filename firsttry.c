/**
 * @file firsttry.c
 * @brief Main application entry point and RPL network message dispatcher
 *
 * This is the primary application module that:
 * - Initializes the RPL (Routing Protocol for LLN) routing protocol
 * - Manages the NullNet RX callback for receiving and classifying messages
 * - Dispatches messages to appropriate handlers (DIO, DIS, DAO, MSG_UP, MSG_DOWN, etc.)
 * - Implements the trickle timer for efficient DIO broadcasts at the root
 * - Coordinates role-specific behavior (root, access point, node, nurse)
 * - Handles user input via buttons to trigger emergency alerts on nodes
 *
 * @author WSN Lab Group 6
 * @date 2026
 *
 * @defgroup app_main Main Application
 * @{
 */

#include "contiki.h"
#include "net/nullnet/nullnet.h"
#include "net/netstack.h"
#include "net/linkaddr.h"
#include "net/packetbuf.h"
#include "dev/button-hal.h"
#include "dev/leds.h"
#include "rpl_route.h"
#include "rpl_config.h"
#include "rpl_net.h"
#include "rpl_msg.h"
#include "rpl_state.h"
#include "trickle.h"

#include "dio.h"
#include "dis.h"
#include "dao.h"
#include "msg_up.h"
#include "msg_down.h"
#include "external_sensors.h"

#include <string.h>

/**
 * @defgroup app_state Module State Variables
 * @{
 */
static uint8_t node_id;            /**< ID of this node (last byte of link address) */
static linkaddr_t node_lladdr;     /**< Link-layer address of this node */
static char rxbuf[PACKETBUF_SIZE]; /**< Buffer for received message data */
static trickle_t dio_trickle;      /**< Trickle timer instance for DIO transmission */
/** @} */

/**
 * @defgroup app_tx Transmission Functions
 * @brief Functions for sending messages over the network
 * @{
 */

/**
 * @brief Broadcast a message using NullNet
 *
 * Copies the string into the NullNet buffer and transmits a link-layer
 * broadcast so that all neighbors receive the packet.
 *
 * @param msg Null-terminated message string to broadcast
 *
 * @note The message is copied into the NullNet buffer and immediately
 *       transmitted. The caller retains responsibility for the original buffer.
 */
void rpl_broadcast(const char *msg)
{
  nullnet_buf = (uint8_t *)msg;
  nullnet_len = strlen(msg);
  NETSTACK_NETWORK.output(NULL);
}

/**
 * @brief Send a unicast frame to a destination address
 *
 * Places the payload in the NullNet buffer and transmits it to the specified
 * link-layer address. This is a unicast transmission; only the destination
 * node will receive the message.
 *
 * @param buf   Pointer to payload buffer to transmit
 * @param len   Number of bytes to send
 * @param dest  Destination link-layer address
 *
 * @note The message is copied into the NullNet buffer and immediately
 *       transmitted. The caller retains responsibility for the original buffer.
 *
 * @see rpl_broadcast() for broadcasting to all neighbors
 */
void rpl_unicast_addr(const void *buf, uint16_t len, const linkaddr_t *dest)
{
  nullnet_buf = (uint8_t *)buf;
  nullnet_len = len;
  NETSTACK_NETWORK.output(dest);
}

/** @} */

/**
 * @defgroup app_rx Reception and Message Dispatch
 * @brief Functions for receiving and processing network messages
 * @{
 */

/**
 * @brief NullNet RX callback that classifies and dispatches RPL messages
 *
 * This is the main entry point for received messages. The function:
 * - Copies incoming data into the receive buffer
 * - Classifies the message type using rpl_msg_classify()
 * - Invokes the appropriate handler based on message type
 * - Handles trickle timer resets for DIS and TRICKLE messages
 *
 * **Supported Message Types:**
 * - MSG_DIO: DODAG Information Object (routing information)
 * - MSG_DIS: DODAG Information Solicitation (parent search)
 * - MSG_DAO: Destination Advertisement Object (downlink information)
 * - MSG_UP / MSG_UP_ACK: Upward application messages and acknowledgments
 * - MSG_DOWN / MSG_DOWN_ACK: Downward application messages and acknowledgments
 * - MSG_TRICKLE: Trickle control message
 *
 * @param data  Pointer to received payload buffer
 * @param len   Length of received payload in bytes
 * @param src   Source link-layer address of the sender
 * @param dest  Destination link-layer address (unused, included for NullNet callback compatibility)
 *
 * @note This function is registered as the NullNet input callback and is invoked
 *       by the network stack whenever a packet is received.
 *
 * @see rpl_msg_classify() for message type classification
 * @see dio_handle_rx(), dis_handle_rx(), dao_handle_rx() for message handlers
 */
static void rpl_rx_callback(const void *data, uint16_t len,
                            const linkaddr_t *src,
                            const linkaddr_t *dest)
{
  (void)dest;
  (void)src;

  if (!data)
    return;

  if (len >= sizeof(rxbuf))
    len = sizeof(rxbuf) - 1;

  memcpy(rxbuf, data, len);
  rxbuf[len] = '\0';

  rpl_msg_type_t t = rpl_msg_classify(rxbuf, len);

  switch (t)
  {
  case MSG_DIO:
    /* Routing information from parent or candidate parents */
    dio_handle_rx(rxbuf, len, src);
    break;
  case MSG_DIS:
    /* Parent solicitation - only handled by root and APs */
    if (MY_ROLE != ROLE_NODE && MY_ROLE != ROLE_NURSE)
    {
      if (dis_handle_rx(rxbuf, len, src))
      {
        /* Reset trickle to accelerate DIO response */
        trickle_reset(&dio_trickle);
      }
    }
    break;
  case MSG_DAO:
    /* Downlink routing information from child nodes */
    if(dao_handle_rx(rxbuf, len, src) && MY_ROLE == ROLE_ROOT)
    {
      /* Reset trickle to propagate updated routing info */
      trickle_reset(&dio_trickle);
    }
    break;
  case MSG_UP:
    /* Upward emergency or heartbeat message */
    msg_up_handle_rx(rxbuf, len, src);
    break;
  case MSG_UP_ACK:
    /* Acknowledgment for upward message */
    msg_up_handle_rx(rxbuf, len, src);
    break;
  case MSG_DOWN:
    /* Downward control message from root */
    msg_down_handle_rx(rxbuf, len, src);
    break;
  case MSG_DOWN_ACK:
    /* Acknowledgment for downward message */
    msg_down_handle_rx(rxbuf, len, src);
    break;
  case MSG_TRICKLE:
    /* Trickle control - reset at root, handle at APs */
    if (MY_ROLE == ROLE_ROOT)
    {
      trickle_reset(&dio_trickle);
    }
    if (MY_ROLE == ROLE_AP)
    {
      msg_trickle_handle_rx(rxbuf, len, src);
    }
    break;
  default:
    /* Unknown message type - ignore */
    break;
  }
}

/** @} */

/**
 * @defgroup app_processes Contiki Processes
 * @{
 */

/**
 * @brief Main role-dependent network coordination process
 *
 * This is the primary process that handles:
 * - Radio and RPL initialization
 * - Root behavior: trickle timer for DIO broadcasts and route management
 * - Non-root behavior: periodic DIS/DAO transmission, parent health checks
 * - Node-specific: button input handling for emergency alerts
 *
 * **Role-Specific Behavior:**
 * - **ROOT**: Sends periodic DIOs via trickle timer, prunes expired and unreachable routes
 * - **ACCESS POINT (AP)**: Sends DIS/DAO, monitors parent, forwards control messages
 * - **NODE**: Sends DIS/DAO, monitors parent, handles button presses for alerts
 * - **NURSE**: Sends DIS/DAO, monitors parent, receives downlink commands
 *
 * **Button Mappings (Nodes only):**
 * - Button 0: Emergency alert (MSG_UP_EMERGENCY)
 * - Button 1: Toilet request (MSG_UP_TOILET)
 * - Button 2: Water request (MSG_UP_WATER)
 * - Button 3: Nurse assistance (MSG_UP_NURSE)
 */
PROCESS(role_process, "Role-dependent process");

/**
 * @brief Button/buzzer pulse process
 *
 * Handles button input processing and buzzer feedback. Defined elsewhere
 * and included in autostart list.
 */
PROCESS_NAME(button_buzzer_pulse_process);

/**
 * @brief Heartbeat monitor process
 *
 * Monitors node health status and sends periodic heartbeat messages.
 * Defined elsewhere and included in autostart list.
 */
PROCESS_NAME(heartbeat_monitor_process);

AUTOSTART_PROCESSES(&role_process, &button_buzzer_pulse_process, &heartbeat_monitor_process);

/**
 * @brief Main process thread implementation
 *
 * Initializes all subsystems and implements role-specific event loops.
 * See role_process documentation for detailed behavior description.
 */
PROCESS_THREAD(role_process, ev, data)
{
  static struct etimer dis_timer;          /**< Timer for periodic DIS transmission */
  static struct etimer dao_timer;          /**< Timer for periodic DAO transmission */
  static struct etimer route_prune_timer;  /**< Timer for route maintenance */
  static struct etimer parent_check_timer; /**< Timer for parent health checks */
  static uint8_t dao_seq = 0;              /**< DAO sequence number counter */

  PROCESS_BEGIN();

  /* ================================================================ */
  /* Initialization Phase */
  /* ================================================================ */

  /* Determine node role from link address */
  role_init_from_lladdr(&linkaddr_node_addr);

  /* Configure radio parameters */
  NETSTACK_CONF_RADIO.set_value(RADIO_PARAM_CHANNEL, RF_CHANNEL);
  NETSTACK_CONF_RADIO.set_value(RADIO_PARAM_TXPOWER, RF_TXPOWER);

  /* Store node identification */
  node_id = linkaddr_node_addr.u8[LINKADDR_SIZE - 1];
  linkaddr_copy(&node_lladdr, &linkaddr_node_addr);

  /* Initialize RPL state and routing */
  rpl_state_init(node_id, &node_lladdr, 1);
  rpl_route_init();

  /* Register message receive callback */
  nullnet_set_input_callback(rpl_rx_callback);

  /* Enable RX address filtering for energy efficiency */
  int rxmode = 0;
  NETSTACK_RADIO.get_value(RADIO_PARAM_RX_MODE, &rxmode);
  rxmode |= RADIO_RX_MODE_ADDRESS_FILTER;
  NETSTACK_RADIO.set_value(RADIO_PARAM_RX_MODE, rxmode);

  /* ================================================================ */
  /* ROOT Node Event Loop */
  /* ================================================================ */

  if (MY_ROLE == ROLE_ROOT)
  {
    etimer_set(&route_prune_timer, PRUNE_PERIOD);

    /* Initialize trickle timer with configured parameters */
    trickle_init(&dio_trickle, DIO_TRICKLE_IMIN, DIO_TRICKLE_DOUBLINGS);

    while (1)
    {
      PROCESS_WAIT_EVENT();

      /* Send DIO if trickle timer indicates transmission */
      if (trickle_tx_ready(&dio_trickle))
      {
        leds_single_on(LEDS_LED1);
        trickle_mark_tx_sent(&dio_trickle);

        dio_send(rpl_state_get_lladdr(),
                 rpl_state_get_version(),
                 rpl_state_get_rank(),
                 rpl_state_get_parent_lladdr());

        leds_off(LEDS_LED1);
      }

      /* Handle trickle interval expiration */
      if (trickle_interval_ready(&dio_trickle))
      {
        trickle_on_interval_expired(&dio_trickle);
      }

      /* Prune routes periodically */
      if (etimer_expired(&route_prune_timer))
      {
        if(rpl_route_prune_expired()){
          trickle_reset(&dio_trickle);
        }
        if(rpl_route_prune_unreachable()){
          trickle_reset(&dio_trickle);
        }
        rpl_route_print_gui();
        etimer_reset(&route_prune_timer);
      }
    }
  }

  /* ================================================================ */
  /* Non-Root Event Loop (AP, Node, Nurse) */
  /* ================================================================ */

  etimer_set(&dao_timer, DAO_PERIOD);
  etimer_set(&dis_timer, DIS_PERIOD);
  etimer_set(&parent_check_timer, PARENT_CHECK_PER);

  /* Send initial DIS to find parent */
  dis_send(rpl_state_get_lladdr());

  while (1)
  {
    PROCESS_WAIT_EVENT();

    const linkaddr_t *my_parent = rpl_state_get_parent_lladdr();

    /* Send DIS if no parent found yet */
    if (etimer_expired(&dis_timer))
    {
      if (my_parent->u8[LINKADDR_SIZE - 1] == 0)
      {
        dis_send(rpl_state_get_lladdr());
      }
      etimer_reset(&dis_timer);
    }

    /* Send DAO to register downlink information */
    if (etimer_expired(&dao_timer))
    {
      const linkaddr_t *p = rpl_state_get_parent_lladdr();
      if (p != NULL)
      {
        dao_send_target_parent(rpl_state_get_lladdr(), p, dao_seq++, p);
      }
      etimer_reset(&dao_timer);
    }

    /* Check parent health and timeout */
    if (etimer_expired(&parent_check_timer))
    {
      etimer_reset(&parent_check_timer);
      dio_parent_timeout_check();
    }

    /* ============================================================ */
    /* Node-Specific: Handle Button Events */
    /* ============================================================ */

    if (MY_ROLE == ROLE_NODE)
    {
      msg_up_init();
      
      
      if (ev == button_hal_press_event)
      {
        button_hal_button_t *btn = (button_hal_button_t *)data;

        if (btn == button_hal_get_by_id(BUTTON_HAL_ID_BUTTON_ZERO))
        {
          /* Emergency alert */
          msg_up_send(MSG_UP_EMERGENCY);
          leds_single_on(LEDS_LED1);
        }
        else if (btn == button_hal_get_by_id(BUTTON_HAL_ID_BUTTON_ONE))
        {
          /* Toilet request */
          msg_up_send(MSG_UP_TOILET);
          leds_single_on(LEDS_LED2);
        }
        else if (btn == button_hal_get_by_id(BUTTON_HAL_ID_BUTTON_TWO))
        {
          /* Water request */
          msg_up_send(MSG_UP_WATER);
          leds_single_on(LEDS_LED3);
        }
        else if (btn == button_hal_get_by_id(BUTTON_HAL_ID_BUTTON_THREE))
        {
          /* Nurse assistance / clear alerts */
          msg_up_send(MSG_UP_NURSE);
          leds_off(LEDS_ALL);
        }
      }
    }
  }

  PROCESS_END();
}

/** @} */
/** @} */
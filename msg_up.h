/**
 * @file msg_up.h
 * @brief Upward message routing from nodes to root
 *
 * This module handles the routing of alert and heartbeat messages
 * from BED_NODE devices upward through the DODAG to reach the
 * root node.
 */

#ifndef MSG_UP_H
#define MSG_UP_H

#include <stdint.h>
#include "net/linkaddr.h"

/**
 * @brief Message types for upward communication
 */
typedef enum
{   
    MSG_UP_HEARTBEAT_EMERGENCY = 0, /**< Emergency heartbeat (highest priority) */
    MSG_UP_EMERGENCY , /**< Emergency alert (highest priority) */
    MSG_UP_TOILET,        /**< Toilet call request */
    MSG_UP_WATER,         /**< Water request */
    MSG_UP_NURSE,         /**< Nurse call */
    MSG_UP_HEARTBEAT,     /**< Heartbeat measurement */
} msg_up_type_t;

/**
 * @brief Send an upward message of specified type
 *
 * Creates and queues an upward message to be transmitted to the root.
 * Messages are retransmitted if acknowledgment is not received.
 *
 * @param type The type of message to send
 */
void msg_up_send(msg_up_type_t type);

/**
 * @brief Send heartbeat message with BPM value
 *
 * Sends a heartbeat message containing the current beats-per-minute
 * value to the root node.
 *
 * @param bpm Current heartbeat rate in beats per minute
 */
void msg_up_send_heartbeat(uint16_t bpm);

/**
 * @brief Initialize upward message system
 *
 * Must be called once at startup to initialize message queues
 * and retry mechanisms.
 */
void msg_up_init(void);

/**
 * @brief Handle received upward message
 *
 * Processes upward messages, forwards them toward root if needed,
 * or handles them at intermediate nodes (nurse display).
 *
 * @param buf       Received message buffer
 * @param len       Message length
 * @param src_ll    Sender's link-layer address
 * @return          1 if message was handled, 0 otherwise
 */
int msg_up_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src_ll);

#endif
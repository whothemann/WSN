/**
 * @file msg_down.h
 * @brief Downward message routing from root to specific nodes
 *
 * This module handles the routing of messages from the root node
 * downward through the DODAG to reach specific target devices
 * identified by labels.
 */

#ifndef MSG_DOWN_H
#define MSG_DOWN_H

#include <stdint.h>
#include "net/linkaddr.h"

/**
 * @brief Send a message downward to a ROLE_NURSE
 *
 * Root node builds a downward route to reach a nurse station
 * and sends the bed_label message for LED control, along with
 * information about whether this is a heartbeat emergency.
 *
 * @param bed_label         Null-terminated string identifying the target bed
 *                          (e.g., "BED1", "BED2", or "EMPTY")
 * @param heart_emergency   Non-zero if this is a heartbeat emergency,
 *                          triggering continuous buzzer feedback at nurse station
 */
void msg_down_send_to_nurse(const char *bed_label, int heart_emergency);

/**
 * @brief Handle received downward message
 *
 * Processes a message traveling down the tree:
 * - Strips own hop from the message
 * - Forwards remaining hops to next node in path
 * - Nurse nodes apply the received label to LED display
 *
 * @param buf       Received message buffer
 * @param len       Message length
 * @param src_ll    Sender's link-layer address
 * @return          1 if message was handled, 0 otherwise
 */
int msg_down_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src_ll);

#endif /* MSG_DOWN_H */
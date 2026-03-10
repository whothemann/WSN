/**
 * @file dis.h
 * @brief DIS (DODAG Information Solicitation) message handling for RPL
 *
 * This module handles the creation and processing of DIS messages
 * used to request DODAG information from neighbors in the RPL
 * routing protocol.
 */

#pragma once
#include <stdint.h>
#include "net/linkaddr.h"

/**
 * @brief Send a DIS message
 *
 * Broadcasts a DODAG Information Solicitation message to request
 * DIO messages from neighbors. Used when a node needs topology
 * information or has lost connection.
 *
 * @param src_id The sender's node identifier (link-layer address)
 */
void dis_send(const linkaddr_t *src_id);

/**
 * @brief Handle received DIS message
 *
 * Processes an incoming DIS message. Nodes that have joined a DODAG
 * respond with a DIO containing their current rank and parent information.
 * Root and access points always respond.
 *
 * @param buf       Pointer to the received message buffer
 * @param len       Length of the received message
 * @param src       Pointer to the sender's link-layer address
 * @return          1 if message was handled, 0 otherwise
 */
int dis_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src);
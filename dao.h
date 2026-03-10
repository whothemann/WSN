/**
 * @file dao.h
 * @brief DAO (DODAG Advertisement Object) message handling for RPL
 *
 * This module handles the creation and processing of DAO messages
 * in the RPL protocol implementation. DAOs are used to advertise
 * paths towards leaves in the DODAG.
 */

#pragma once
#include <stdint.h>
#include "net/linkaddr.h"

/**
 * @brief Send a DAO message with target and parent information
 *
 * Constructs a DAO message containing target and parent link-layer
 * addresses along with a sequence number, then sends it to a specified
 * destination using unicast.
 *
 * @param target    Pointer to the target node's link-layer address
 * @param parent    Pointer to the parent node's link-layer address
 * @param seq       Sequence number for this DAO message
 * @param to_ll     Pointer to the destination link-layer address for unicast
 */
void dao_send_target_parent(const linkaddr_t *target,
                            const linkaddr_t *parent,
                            uint8_t seq,
                            const linkaddr_t *to_ll);

/**
 * @brief Handle received DAO message
 *
 * Parses incoming DAO message and updates routing information.
 * At the root, adds parent information to the routing table.
 * At other nodes, forwards the DAO upwards towards the root.
 *
 * @param buf       Pointer to the received message buffer
 * @param len       Length of the received message
 * @param src_ll    Pointer to the sender's link-layer address
 * @return          1 if message was handled, 0 otherwise
 */
int dao_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src_ll);
/**
 * @file dio.h
 * @brief DIO (DODAG Information Object) message handling for RPL
 *
 * This module handles the creation and processing of DIO messages
 * used for topology dissemination and parent selection in the RPL
 * routing protocol.
 */

#pragma once
#include <stdint.h>
#include "net/linkaddr.h"

/**
 * @brief Handle received DIO message
 *
 * Processes incoming DIO from a neighbor, updating parent candidate
 * list based on rank and RSSI metrics. Performs parent selection using
 * composite scoring with hysteresis to avoid frequent parent switches.
 * Access points relay DIOs to other nodes.
 *
 * @param buf       Pointer to the received message buffer
 * @param len       Length of the received message
 * @param src       Pointer to the sender's link-layer address
 * @return          1 if message was handled, 0 otherwise
 */
int dio_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src);

/**
 * @brief Send a DIO message
 *
 * Creates and broadcasts a DIO message containing node identity,
 * version, rank, and parent information. This is used by the root
 * and access points to disseminate topology information.
 *
 * @param src_id            The sender's node identifier (link-layer address)
 * @param ver               Current DAG version number
 * @param rank              Node's rank in the DODAG
 * @param parent_lladdr     Parent's link-layer address (NULL for root)
 */
void dio_send(const linkaddr_t *src_id, uint8_t ver, uint16_t rank, const linkaddr_t *parent_lladdr);

/**
 * @brief Check for timed-out parent candidates
 *
 * Performs periodic maintenance:
 * - Removes stale candidate parents that haven't been heard from
 * - Validates that the current parent is still reachable
 * - Triggers rank reset if current parent disappears
 */
void dio_parent_timeout_check(void);
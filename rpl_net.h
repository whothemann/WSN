/**
 * @file rpl_net.h
 * @brief Network transmission helpers for RPL
 *
 * Provides broadcast and unicast message transmission functions
 * implemented in the main application file (firsttry.c).
 */

#pragma once
#include <stdint.h>

/**
 * @brief Broadcast a message to all neighbors
 *
 * Transmits a message using link-layer broadcast so all nodes
 * in radio range can receive it.
 *
 * @param msg   Pointer to the message string
 */
void rpl_broadcast(const char *msg);

/**
 * @brief Send a unicast message to a specific address
 *
 * Transmits a message to a specific destination link-layer address.
 * Used for node-to-node communication.
 *
 * @param buf       Pointer to the message buffer
 * @param len       Length of the message in bytes
 * @param dest      Destination link-layer address
 */
void rpl_unicast_addr(const void *buf, uint16_t len, const linkaddr_t *dest);
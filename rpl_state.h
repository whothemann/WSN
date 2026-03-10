/**
 * @file rpl_state.h
 * @brief RPL protocol state management
 *
 * This module manages the runtime state of the RPL protocol,
 * including version, rank, parent, and node identity information.
 */

#pragma once

#include <stdint.h>
#include "net/linkaddr.h"

/**
 * @brief Initialize RPL state
 *
 * Must be called once at startup with the node's configuration.
 * Root nodes start with rank 0, others with rank 0xFFFF (unknown).
 *
 * @param node_id          Unique node identifier
 * @param lladdr            Link-layer address of this node
 * @param initial_version   Initial DODAG version number
 */
void rpl_state_init(uint8_t node_id, const linkaddr_t *lladdr, uint8_t initial_version);

/**
 * @brief Get node's unique identifier
 * @return The node ID
 */
uint8_t rpl_state_get_node_id(void);

/**
 * @brief Get current DODAG version
 * @return The DODAG version number
 */
uint8_t rpl_state_get_version(void);

/**
 * @brief Get node's current rank in DODAG
 * @return Rank value (0 for root, 0xFFFF for unknown)
 */
uint16_t rpl_state_get_rank(void);

/**
 * @brief Get current parent's link-layer address
 * @return Pointer to parent's address (null if no parent)
 */
const linkaddr_t *rpl_state_get_parent_lladdr(void);

/**
 * @brief Get this node's link-layer address
 * @return Pointer to this node's address
 */
const linkaddr_t *rpl_state_get_lladdr(void);

/**
 * @brief Set node's unique identifier
 * @param id The new node ID
 */
void rpl_state_set_node_id(uint8_t id);

/**
 * @brief Set current DODAG version
 * @param ver The new version number
 */
void rpl_state_set_version(uint8_t ver);

/**
 * @brief Set node's rank
 * @param rank The new rank value
 */
void rpl_state_set_rank(uint16_t rank);

/**
 * @brief Set parent's link-layer address
 * @param parent_lladdr Pointer to the parent's address
 */
void rpl_state_set_parent_lladdr(const linkaddr_t *parent_lladdr);

/**
 * @brief Set this node's link-layer address
 * @param lladdr Pointer to the address
 */
void rpl_state_set_lladdr(const linkaddr_t *lladdr);

/**
 * @brief Reset state as if a new generation started
 *
 * Clears parent information and resets rank to unknown state.
 * Called when a new DODAG version is detected.
 *
 * @param new_version The new DODAG version number
 */
void rpl_state_reset(uint8_t new_version);
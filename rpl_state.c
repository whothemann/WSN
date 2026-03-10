/**
 * @file rpl_state.c
 * @brief RPL state management and node identity tracking
 *
 * This module maintains the local RPL state for this node, including its
 * identity, rank in the DODAG (Directed Acyclic Graph), version number,
 * and the address of its preferred parent in the tree.
 *
 * **Core Responsibilities:**
 * - Store node identification (ID and link-layer address)
 * - Track DODAG version and generation
 * - Maintain node rank (distance metric to root)
 * - Store parent node address
 * - Provide getters and setters for all state variables
 *
 * **State Lifecycle:**
 * - Initialized once at startup with rpl_state_init()
 * - Reset on DODAG version change with rpl_state_reset()
 * - Updated as DIO/DAO messages are processed
 * - Read by other modules to determine behavior and message creation
 *
 * @author WSN Lab Group 6
 * @date 2026
 *
 * @defgroup rpl_state RPL State Management
 * @{
 */

#include "rpl_state.h"
#include "rpl_config.h"
#include "rpl_route.h"
#include "net/linkaddr.h"

/**
 * @defgroup rpl_state_vars State Variables
 * @brief RPL state maintained by this node
 * @{
 */

/**
 * @brief Unique node identifier
 *
 * Typically derived from the last bytes of the MAC address.
 * Used to distinguish this node in the network.
 */
static uint8_t s_node_id = 0;

/**
 * @brief Current DODAG version number
 *
 * Incremented by root when starting a new generation.
 * Non-root nodes follow the root's version.
 * Used to detect when a new DODAG has been started.
 */
static uint8_t s_version = 1;

/**
 * @brief Node's rank in the DODAG
 *
 * Represents the distance/hop count to the root:
 * - 0 for the root node
 * - 0xFFFF for unattached nodes (no parent yet)
 * - Intermediate values for attached nodes (calculated as parent_rank + rank_increase)
 *
 * Lower values indicate closer to root (better position in tree).
 */
static uint16_t s_rank = 0xFFFF;

/**
 * @brief This node's link-layer address
 *
 * Set at initialization and used for message construction and routing lookups.
 * 8 bytes (IEEE 802.15.4 extended address).
 */
static linkaddr_t s_lladdr;

/**
 * @brief Address of the preferred parent node
 *
 * Updated when receiving DIOs. Used as the next-hop for upward messages.
 * Initialized to null address (all zeros) until a parent is selected.
 */
static linkaddr_t s_parent_lladdr = {{0}};

/** @} */

/**
 * @defgroup rpl_state_lifecycle Lifecycle Management
 * @brief Functions for initialization and state transitions
 * @{
 */

/**
 * @brief Initialize RPL state at startup
 *
 * Sets up the initial state for this node, establishing its identity and rank.
 * Root nodes are initialized with rank 0 (at the top of the tree),
 * while non-root nodes start with unknown rank (0xFFFF) until they join the DODAG.
 *
 * **State After Initialization:**
 * - Node ID and address are stored for identity
 * - Version is set to initial value (usually 1)
 * - Parent address is cleared (null)
 * - Rank is set based on role: 0 for root, 0xFFFF for others
 *
 * **Preconditions:**
 * - MY_ROLE must be initialized before this is called
 * - lladdr must point to a valid 8-byte link address
 *
 * @param node_id Unique node identifier (usually derived from MAC address)
 * @param lladdr Pointer to node's link-layer address (8 bytes)
 * @param initial_version Starting DODAG version number (typically 1)
 *
 * @see rpl_state_reset() for handling DODAG version changes
 * @see MY_ROLE
 */
void rpl_state_init(uint8_t node_id, const linkaddr_t *lladdr, uint8_t initial_version)
{
  s_node_id = node_id;
  linkaddr_copy(&s_lladdr, lladdr);
  s_version = initial_version;
  linkaddr_copy(&s_parent_lladdr, &(linkaddr_null));

  /* Root starts at rank 0 (top of tree); others start unattached */
  s_rank = (MY_ROLE == ROLE_ROOT) ? 0 : 0xFFFF;
}

/**
 * @brief Reset state for a new DODAG generation
 *
 * Called when a new DODAG version is detected (version number increased).
 * Clears the current parent and resets rank to allow rejoin via new parent selection.
 * This essentially forgets the old tree structure and allows formation of a new one.
 *
 * **Effect:**
 * - Version number is updated
 * - Parent address is cleared (node becomes unattached)
 * - Rank is reset to initial state
 *
 * **Typical Trigger:**
 * - Received a DIO with higher version than current s_version
 * - Root initiated a new DODAG generation (usually after topology change)
 *
 * @param new_version The new DODAG version number from the root
 *
 * @see rpl_state_set_version() for non-reset version changes
 * @see firsttry.c for DIO reception and version handling
 */
void rpl_state_reset(uint8_t new_version)
{
  /* Update to new generation */
  s_version = new_version;

  /* Clear parent and reset rank to initial state */
  linkaddr_copy(&s_parent_lladdr, &(linkaddr_null));
  s_rank = (MY_ROLE == ROLE_ROOT) ? 0 : 0xFFFF;
}

/** @} */

/**
 * @defgroup rpl_state_getters Getter Functions
 * @brief Functions to read RPL state variables
 * @{
 */

/**
 * @brief Get this node's unique identifier
 *
 * @return Node ID (8-bit value)
 *
 * @see rpl_state_set_node_id()
 */
uint8_t rpl_state_get_node_id(void)
{
  return s_node_id;
}

/**
 * @brief Get current DODAG version number
 *
 * @return DODAG version (higher value indicates newer generation)
 *
 * @see rpl_state_set_version()
 * @see rpl_state_reset()
 */
uint8_t rpl_state_get_version(void)
{
  return s_version;
}

/**
 * @brief Get this node's rank in the DODAG
 *
 * The rank represents the node's position in the tree hierarchy:
 * - 0 = root node (top)
 * - 1-32767 = distance to root (lower is better)
 * - 0xFFFF = unattached (no parent found yet)
 *
 * @return Node's rank value
 *
 * @see rpl_state_set_rank()
 */
uint16_t rpl_state_get_rank(void)
{
  return s_rank;
}

/**
 * @brief Get address of the preferred parent node
 *
 * Returns the link-layer address of the node selected as parent.
 * Used for sending upward messages and determining tree connectivity.
 *
 * @return Pointer to parent address (8 bytes), or all-zeros if unattached
 *
 * @see rpl_state_set_parent_lladdr()
 */
const linkaddr_t *rpl_state_get_parent_lladdr(void)
{
  return &s_parent_lladdr;
}

/**
 * @brief Get this node's link-layer address
 *
 * Returns the IEEE 802.15.4 extended address assigned to this node.
 * Used in message headers, routing decisions, and device identification.
 *
 * @return Pointer to this node's address (8 bytes)
 *
 * @see rpl_state_set_lladdr()
 */
const linkaddr_t *rpl_state_get_lladdr(void)
{
  return &s_lladdr;
}

/** @} */

/**
 * @defgroup rpl_state_setters Setter Functions
 * @brief Functions to update RPL state variables
 * @{
 */

/**
 * @brief Set this node's unique identifier
 *
 * Typically called during initialization. The node ID is used for debugging
 * and diagnostics but is distinct from the link-layer address.
 *
 * @param id New node ID value
 *
 * @see rpl_state_get_node_id()
 */
void rpl_state_set_node_id(uint8_t id)
{
  s_node_id = id;
}

/**
 * @brief Set the DODAG version number
 *
 * Used to follow version changes announced by the root in DIOs.
 * Does NOT reset rank or parent like rpl_state_reset() does.
 * For generation changes, use rpl_state_reset() instead.
 *
 * @param ver New DODAG version number
 *
 * @see rpl_state_get_version()
 * @see rpl_state_reset() for handling DODAG resets
 */
void rpl_state_set_version(uint8_t ver)
{
  s_version = ver;
}

/**
 * @brief Set this node's rank in the DODAG
 *
 * Updates the rank based on parent's rank and link metrics.
 * Rank is typically calculated as: parent_rank + RANK_INCREMENT
 *
 * @param rank New rank value (0 for root, 0xFFFF for unattached)
 *
 * @see rpl_state_get_rank()
 */
void rpl_state_set_rank(uint16_t rank)
{
  s_rank = rank;
}

/**
 * @brief Set the preferred parent node address
 *
 * Updates the parent address when a better parent is selected via DIO processing.
 * The parent is the next-hop for upward messages toward the root.
 *
 * @param parent Pointer to new parent address (8 bytes), or NULL for no parent
 *
 * @see rpl_state_get_parent_lladdr()
 */
void rpl_state_set_parent_lladdr(const linkaddr_t *parent)
{
  linkaddr_copy(&s_parent_lladdr, parent);
}

/**
 * @brief Set this node's link-layer address
 *
 * Typically called only during initialization. The address is fundamental
 * to network identity and used throughout message construction.
 *
 * @param lladdr Pointer to new link-layer address (8 bytes)
 *
 * @see rpl_state_get_lladdr()
 */
void rpl_state_set_lladdr(const linkaddr_t *lladdr)
{
  linkaddr_copy(&s_lladdr, lladdr);
}

/** @} */
/** @} */
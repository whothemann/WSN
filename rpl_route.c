/**
 * @file rpl_route.c
 * @brief RPL routing table management and path building
 *
 * This module manages the routing information collected from upward DAO messages,
 * maintains the parent-child relationships for the RPL tree, and provides path
 * construction services for downward message delivery.
 *
 * **Core Functionality:**
 * - Stores parent-child routing relationships from all network nodes
 * - Builds end-to-end paths from root to any destination
 * - Maintains route timeouts and prunes stale routes
 * - Maps device MAC addresses to roles and human-readable labels
 * - Provides topology visualization for GUI
 *
 * **Data Structures:**
 * - Parent table: stores node->parent relationships with timestamps
 * - Role map: maps MAC addresses to device roles and names
 *
 * @author WSN Lab Group 6
 * @date 2026
 *
 * @defgroup rpl_route RPL Route Management
 * @{
 */

#include "rpl_route.h"
#include "rpl_state.h"
#include "rpl_config.h"
#include "net/linkaddr.h"
#include "sys/clock.h"
#include <stdio.h>
#include <string.h>

/**
 * @defgroup rpl_route_config Configuration Constants
 * @{
 */
#define MAX_ROUTES 32    /**< Maximum number of nodes in routing table */
#define MAX_PATH_HOPS 16 /**< Maximum path depth (tree height limit) */
/** @} */

/**
 * @defgroup rpl_route_debug Debug Configuration
 * @{
 */
#ifndef GUI_TOPO_DEBUG
#define GUI_TOPO_DEBUG 0 /**< Enable debug output for topology visualization */
#endif

#ifndef GUI_TOPO_ALLOW_UNKNOWN
#define GUI_TOPO_ALLOW_UNKNOWN 0 /**< Output links even if role mapping is unknown */
#endif
/** @} */

/**
 * @defgroup rpl_route_data Data Structures
 * @{
 */

/**
 * @brief Routing table entry for parent-child relationships
 *
 * Stores the parent node for a given child node, with timestamp for timeout management.
 */
typedef struct
{
  uint8_t used;           /**< Entry is in use */
  linkaddr_t node;        /**< Child node address */
  linkaddr_t parent;      /**< Parent node address */
  clock_time_t last_seen; /**< Timestamp of last update */
} parent_entry_t;

static parent_entry_t parents[MAX_ROUTES]; /**< Routing table buffer */

/** @} */

/**
 * @defgroup rpl_route_basic Basic Route Operations
 * @brief Core routing table management functions
 * @{
 */

/**
 * @brief Initialize the routing table
 *
 * Clears all routing entries. Must be called once at startup.
 */
void rpl_route_init(void)
{
  memset(parents, 0, sizeof(parents));
}

/**
 * @brief Add or update a parent-child relationship in the routing table
 *
 * Stores that the given node has the specified parent. If the node already
 * exists in the table, updates its parent and timestamp. Otherwise, inserts
 * a new entry.
 *
 * @param node Pointer to the child node address
 * @param parent Pointer to the parent node address
 * @return 1 if successful (inserted or updated), 0 if table is full
 *
 * @see rpl_route_get_parent() to retrieve parent information
 */
int rpl_route_add_parent(const linkaddr_t *node, const linkaddr_t *parent)
{
  if(!node || !parent) {
    return 0;
  }

  /* If entry exists: update only if parent changed */
  for(int i = 0; i < MAX_ROUTES; i++) {
    if(parents[i].used && linkaddr_cmp(&parents[i].node, node)) {

      /* Always refresh last_seen */
      parents[i].last_seen = clock_time();

      /* Only update if parent actually changed */
      if(linkaddr_cmp(&parents[i].parent, parent)) {
        return 0; /* no change */
      }

      linkaddr_copy(&parents[i].parent, parent);
      return 1; /* changed */
    }
  }

  /* No entry yet: insert new entry */
  for(int i = 0; i < MAX_ROUTES; i++) {
    if(!parents[i].used) {
      parents[i].used = 1;
      linkaddr_copy(&parents[i].node, node);
      linkaddr_copy(&parents[i].parent, parent);
      parents[i].last_seen = clock_time();
      return 1; /* new */
    }
  }

  return 0; /* Table full */
}


/**
 * @brief Lookup the parent of a given node
 *
 * Searches the routing table for the parent of the specified node.
 *
 * @param node Pointer to the child node to look up
 * @param parent_out Pointer to output buffer for parent address (may be NULL)
 * @return 1 if node found in table, 0 if not found
 *
 * @see rpl_route_add_parent() to add entries
 */
int rpl_route_get_parent(const linkaddr_t *node, linkaddr_t *parent_out)
{
  if (!node)
    return 0;

  for (int i = 0; i < MAX_ROUTES; i++)
  {
    if (parents[i].used && linkaddr_cmp(&parents[i].node, node))
    {
      if (parent_out)
        linkaddr_copy(parent_out, &parents[i].parent);
      return 1;
    }
  }
  return 0;
}

/** @} */

/**
 * @defgroup rpl_route_path Path Construction
 * @brief Functions for building end-to-end paths
 * @{
 */

/**
 * @brief Build a path from root to destination node
 *
 * Uses parent pointers to walk from the destination node back to the root,
 * then reverses the path to get root->...->destination order.
 * The returned path includes all intermediate hops but excludes the root itself.
 *
 * **Example:**
 * If the tree is: ROOT -> AP1 -> NODE1, and we call with dest=NODE1,
 * the hops_out will contain [AP1, NODE1].
 *
 * @param dest Pointer to destination node address
 * @param hops_out Buffer to store the path (array of linkaddr_t)
 * @param max_hops Maximum number of hops buffer can hold
 * @param hop_count_out Pointer to output variable for actual hop count
 * @return 1 if path was successfully built, 0 if failed (incomplete path, invalid dest, etc.)
 *
 * @note Returns 0 if any intermediate node's parent is missing or if a loop is detected
 * @note The path always starts from a node adjacent to the root
 *
 * @see rpl_route_add_parent() to ensure all nodes are registered
 */
int rpl_route_build_path(const linkaddr_t *dest,
                         linkaddr_t *hops_out,
                         uint8_t max_hops,
                         uint8_t *hop_count_out)
{
  if (!dest || !hops_out || !hop_count_out)
    return 0;

  linkaddr_t root;
  linkaddr_copy(&root, rpl_state_get_lladdr());

  /* Walk upwards from dest to root using parent pointers */
  linkaddr_t cur;
  linkaddr_copy(&cur, dest);

  linkaddr_t tmp[MAX_PATH_HOPS];
  uint8_t n = 0;

  while (!linkaddr_cmp(&cur, &root))
  {
    if (n >= MAX_PATH_HOPS || n >= max_hops)
      return 0;

    tmp[n++] = cur; /* Store this hop (walking towards root) */

    linkaddr_t p;
    if (!rpl_route_get_parent(&cur, &p))
    {
      return 0; /* Parent unknown => path incomplete */
    }

    /* Safety: avoid infinite loops */
    if (linkaddr_cmp(&p, &cur))
      return 0;

    linkaddr_copy(&cur, &p);
  }

  /* Reverse tmp into hops_out (convert from dest->root to root->dest) */
  for (uint8_t i = 0; i < n; i++)
  {
    hops_out[i] = tmp[n - 1 - i];
  }

  *hop_count_out = n;
  return 1;
}

/**
 * @defgroup rpl_route_mapping Device Role and Label Mapping
 * @brief Functions for mapping MAC addresses to device roles and human-readable names
 * @{
 */

/**
 * @brief Mapping entry from MAC address to device role and label
 *
 * Provides the lookup table for converting full link-layer addresses
 * to human-readable names and role information.
 */
typedef struct
{
  linkaddr_t addr;   /**< Full MAC/link-layer address */
  uint8_t role;      /**< Device role (ROLE_ROOT, ROLE_AP, ROLE_NODE, ROLE_NURSE) */
  const char *label; /**< Human-readable label (e.g., "BED1", "GATEWAY") */
} role_map_entry_t;

/**
 * @brief Initialize 8-byte link address from bytes
 *
 * Macro to make role map initialization more readable.
 *
 * @param a0-a7 Eight bytes of the address
 */
#define LLADDR8(a0, a1, a2, a3, a4, a5, a6, a7) \
  {                                             \
    .u8 = { a0,                                 \
            a1,                                 \
            a2,                                 \
            a3,                                 \
            a4,                                 \
            a5,                                 \
            a6,                                 \
            a7 }                                \
  }

/**
 * @brief Device role and label mapping table
 *
 * Maps the full MAC addresses of all known devices to their role and label.
 * Update this table with your device MAC addresses.
 */
static const role_map_entry_t ROLE_MAP[] = {
    {LLADDR8(0xf4, 0xce, 0x36, 0x7a, 0x82, 0x93, 0xea, 0xcd), ROLE_ROOT, "GATEWAY"},
    {LLADDR8(0xf4, 0xce, 0x36, 0x19, 0x94, 0x72, 0xc0, 0x67), ROLE_AP, "ACCESS1"},
    {LLADDR8(0xf4, 0xce, 0x36, 0x08, 0x8e, 0x4d, 0x1e, 0xcc), ROLE_AP, "ACCESS2"},
    {LLADDR8(0xf4, 0xce, 0x36, 0xfb, 0x0f, 0xc9, 0x5e, 0xe8), ROLE_NURSE, "NURSE"},
    {LLADDR8(0xf4, 0xce, 0x36, 0x5c, 0x8d, 0x75, 0x33, 0xde), ROLE_NODE, "BED1"},
    {LLADDR8(0xf4, 0xce, 0x36, 0x64, 0x4e, 0x64, 0x2d, 0xae), ROLE_NODE, "BED2"},
    {LLADDR8(0xf4, 0xce, 0x36, 0xf6, 0x29, 0x1c, 0x96, 0x95), ROLE_NODE, "BED3"},
    {LLADDR8(0xf4, 0xce, 0x36, 0xa8, 0xe7, 0xc0, 0x5b, 0xbb), ROLE_NODE, "BED4"},
};

/**
 * @brief Look up device role from link address
 *
 * Searches the role mapping table to find the role associated with a given address.
 *
 * @param a Pointer to the link address
 * @return Device role (ROLE_ROOT, ROLE_AP, ROLE_NODE, ROLE_NURSE), or 0 if unknown
 *
 * @see label_from_lladdr() to get the human-readable label
 */
static const uint8_t role_from_lladdr(const linkaddr_t *a)
{
  for (unsigned i = 0; i < (sizeof(ROLE_MAP) / sizeof(ROLE_MAP[0])); i++)
  {
    if (linkaddr_cmp(a, &ROLE_MAP[i].addr))
    {
      return ROLE_MAP[i].role;
    }
  }
  return 0; /* Unknown */
}

/**
 * @brief Look up human-readable label from link address
 *
 * Searches the role mapping table to find the label associated with a given address.
 *
 * @param a Pointer to the link address
 * @return Label string (e.g., "BED1", "GATEWAY"), or "UNKNOWN" if not found
 *
 * @see role_from_lladdr() to get the device role
 */
static const char *label_from_lladdr(const linkaddr_t *a)
{
  for (unsigned i = 0; i < (sizeof(ROLE_MAP) / sizeof(ROLE_MAP[0])); i++)
  {
    if (linkaddr_cmp(a, &ROLE_MAP[i].addr))
    {
      return ROLE_MAP[i].label;
    }
  }
  return "UNKNOWN";
}

/**
 * @brief Get device address by role
 *
 * Finds the first device with the specified role and returns its address.
 * Useful for finding the unique nurse device or specific APs.
 *
 * @param role Device role to search for (ROLE_ROOT, ROLE_AP, ROLE_NODE, ROLE_NURSE)
 * @param out_addr Pointer to output buffer for the address
 * @return 1 if device found and address returned, 0 if role not found
 *
 * @see rpl_route_get_addr_by_label() to look up by name
 */
int rpl_route_get_addr_by_role(uint8_t role, linkaddr_t *out_addr)
{
  if (!out_addr)
    return 0;

  for (unsigned i = 0; i < (sizeof(ROLE_MAP) / sizeof(ROLE_MAP[0])); i++)
  {
    if (ROLE_MAP[i].role == role)
    {
      if (out_addr)
      {
        linkaddr_copy(out_addr, &ROLE_MAP[i].addr);
      }
      return 1;
    }
  }
  return 0;
}

/**
 * @brief Get device address by label
 *
 * Finds the device with the specified label and returns its address.
 * Useful for sending messages to specific beds or devices by name.
 *
 * @param label Device label to search for (e.g., "BED1", "NURSE")
 * @param out_addr Pointer to output buffer for the address
 * @return 1 if device found and address returned, 0 if label not found
 *
 * @see rpl_route_get_addr_by_role() to look up by role
 */
int rpl_route_get_addr_by_label(const char *label, linkaddr_t *out_addr)
{
  if (!label || !out_addr)
    return 0;

  for (unsigned i = 0; i < (sizeof(ROLE_MAP) / sizeof(ROLE_MAP[0])); i++)
  {
    if (ROLE_MAP[i].label && strcmp(ROLE_MAP[i].label, label) == 0)
    {
      linkaddr_copy(out_addr, &ROLE_MAP[i].addr);
      return 1;
    }
  }
  return 0;
}

uint8_t MY_ROLE = 0;              // 0 = UNKNOWN default
const char *MY_LABEL = "UNKNOWN"; // default

/**
 * @brief Initialize this node's role and label from its link address
 *
 * Uses the role mapping to set global MY_ROLE and MY_LABEL variables.
 * Called at startup to identify the current device.
 *
 * @param a Pointer to this device's link address
 *
 * @see MY_ROLE, MY_LABEL
 */
void role_init_from_lladdr(const linkaddr_t *a)
{
  MY_ROLE = role_from_lladdr(a);
  MY_LABEL = label_from_lladdr(a);
}

/** @} */

/**
 * @defgroup rpl_route_globals Global Variables
 * @{
 */

/**
 * @brief Global variable: this node's role
 *
 * Set during initialization by role_init_from_lladdr().\n
 * Values: ROLE_ROOT, ROLE_AP, ROLE_NODE, ROLE_NURSE, or 0 for unknown.
 *
 * @see role_init_from_lladdr()
 */
extern uint8_t MY_ROLE;

/**
 * @brief Global variable: this node's human-readable label
 *
 * Set during initialization by role_init_from_lladdr().\n
 * Examples: "GATEWAY", "BED1", "NURSE", or "UNKNOWN" if not in ROLE_MAP.
 *
 * @see role_init_from_lladdr()
 */
extern const char *MY_LABEL;

/** @} */

/**
 * @defgroup rpl_route_gui GUI and Visualization\n
 * @brief Functions for topology visualization\n
 * @{
 */

/**
 * @brief Print topology information for GUI visualization
 *
 * Outputs the current network topology in a format suitable for GUI parsing.
 * Iterates through all routing entries and prints parent-child relationships
 * using human-readable device labels.
 *
 * **Output Format:**\n
 * TOPO_BEGIN\n
 * LINK <parent_label> <child_label>\n
 * LINK <parent_label> <child_label>\n
 * ...\n
 * TOPO_END\n
 *
 * **Configuration:**\n
 * - If GUI_TOPO_ALLOW_UNKNOWN is 0 (default): only outputs links where both\n
 *   nodes are known in ROLE_MAP\n
 * - If GUI_TOPO_ALLOW_UNKNOWN is 1: outputs all links, using "UNKNOWN" for\n
 *   unmapped nodes\n
 * - Self-links (node connecting to itself) are always filtered out\n
 *
 * @note Typically called periodically by the root to update GUI with topology
 *
 * @see GUI_TOPO_ALLOW_UNKNOWN
 */
void rpl_route_print_gui(void)
{
  printf("TOPO_BEGIN\n");

  for (int i = 0; i < MAX_ROUTES; i++)
  {
    if (!parents[i].used)
      continue;

    const char *child_label = label_from_lladdr(&parents[i].node);
    const char *parent_label = label_from_lladdr(&parents[i].parent);

#if !GUI_TOPO_ALLOW_UNKNOWN
    /* Only output if both sides are mapped */
    if (strcmp(child_label, "UNKNOWN") == 0)
      continue;
    if (strcmp(parent_label, "UNKNOWN") == 0)
      continue;
#endif

    /* Avoid self-loops */
    if (strcmp(child_label, parent_label) != 0)
    {
      printf("LINK %s %s\n", parent_label, child_label);
    }
  }
  printf("TOPO_END\n");
}

/** @} */

/** @} */

/**
 * @defgroup rpl_route_maint Route Maintenance
 * @brief Functions for pruning and maintaining routing table
 * @{
 */

/**
 * @brief Prune routes that have not been updated within the timeout period
 *
 * Removes entries that haven't been seen for longer than PRUNE_PERIOD.
 * Called periodically by the root to clean up stale routes.
 *
 * @see rpl_route_prune_unreachable() to also remove orphaned nodes
 */
int rpl_route_prune_expired(void)
{
  int trickle = 0;
  clock_time_t now = clock_time();

  for (int i = 0; i < MAX_ROUTES; i++)
  {
    if (parents[i].used)
    {
      if ((clock_time_t)(now - parents[i].last_seen) > PRUNE_PERIOD)
      {
        parents[i].used = 0;
        trickle = 1; /* Pruned an entry */
        
        if(role_from_lladdr(&parents[i].node) == ROLE_NURSE){
          rpl_state_set_version(rpl_state_get_version() + 1);
        }
      }
    }
  }
  return trickle;
}

/**
 * @brief Check if a parent entry exists for the given node
 *
 * Helper function to determine if a node has a valid routing entry.
 *
 * @param n Node address to search for
 * @return 1 if node has an entry in the table, 0 otherwise
 */
static int parent_exists(const linkaddr_t *n)
{
  for (int i = 0; i < MAX_ROUTES; i++)
  {
    if (parents[i].used && linkaddr_cmp(&parents[i].node, n))
      return 1;
  }
  return 0;
}

/**
 * @brief Prune nodes that are unreachable (orphaned)
 *
 * Removes any node whose parent is not in the routing table, indicating
 * a broken path to the root. Iterates until no more changes occur to handle
 * cascading orphans (children of orphaned nodes).
 *
 * **Algorithm:**
 * 1. For each routing entry, check if its parent exists in the table
 * 2. If parent is missing and is not the root, remove the entry
 * 3. Repeat until no more entries are removed
 *
 * @see rpl_route_prune_expired()
 */
int rpl_route_prune_unreachable(void)
{
  linkaddr_t root;
  linkaddr_copy(&root, rpl_state_get_lladdr());

  int changed;
  int trickle = 0;
  do
  {
    changed = 0;
    for (int i = 0; i < MAX_ROUTES; i++)
    {
      if (!parents[i].used)
        continue;

      /* If parent is root, entry is fine */
      if (linkaddr_cmp(&parents[i].parent, &root))
        continue;

      /* If parent entry is missing, drop this node too */
      if (!parent_exists(&parents[i].parent))
      {
        parents[i].used = 0;
        changed = 1;
        trickle = 1;
      }
    }
  } while (changed);
  return trickle;
}

/** @} */

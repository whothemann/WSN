/**
 * @file rpl_route.h
 * @brief Routing table management for RPL protocol
 *
 * This module maintains the routing table at the root node,
 * tracking parent-child relationships in the DODAG.
 */

#pragma once
#include <stdint.h>
#include "net/linkaddr.h"

/**
 * @brief Node role and label information
 *
 * These variables store the role and label of the current node.
 */
extern uint8_t MY_ROLE;
extern const char *MY_LABEL;

/**
 * @brief Initialize the routing table
 *
 * Must be called once at startup to set up data structures.
 */
void rpl_route_init(void);

/**
 * @brief Remove expired routing entries
 *
 * Removes entries that haven't been updated within PRUNE_PERIOD.
 * Called periodically for maintenance.
 */
int rpl_route_prune_expired(void);

/**
 * @brief Remove unreachable nodes from routing table
 *
 * Removes nodes whose parent is not in the routing table,
 * indicating they are unreachable from the root.
 */
int rpl_route_prune_unreachable(void);

/**
 * @brief Add or update a parent entry
 *
 * Records the parent of a given node in the routing table.
 * Updates the entry if it already exists.
 *
 * @param node       Address of the child node
 * @param parent     Address of the parent node
 * @return           1 if successful, 0 if table full
 */
int rpl_route_add_parent(const linkaddr_t *node, const linkaddr_t *parent);

/**
 * @brief Get parent of a node from routing table
 *
 * @param node         Address of the child node to look up
 * @param parent_out   Output buffer for the parent's address
 * @return             1 if found, 0 if not in table
 */
int rpl_route_get_parent(const linkaddr_t *node, linkaddr_t *parent_out);

/**
 * @brief Get address of node with specific role
 *
 * Searches the node mapping to find a node with the given role.
 *
 * @param role       Role to search for
 * @param out_addr   Output buffer for the found address
 * @return           1 if found, 0 otherwise
 */
int rpl_route_get_addr_by_role(uint8_t role, linkaddr_t *out_addr);

/**
 * @brief Get address of node by label
 *
 * Searches the node mapping to find a node with the given label.
 *
 * @param label      Label string to search for
 * @param out_addr   Output buffer for the found address
 * @return           1 if found, 0 otherwise
 */
int rpl_route_get_addr_by_label(const char *label, linkaddr_t *out_addr);

/**
 * @brief Build a path from root to destination
 *
 * Uses parent pointers to construct the hop-by-hop path from root
 * to the destination node.
 *
 * @param dest           Destination node address
 * @param hops_out       Output array for the path hops
 * @param max_hops       Maximum number of hops to return
 * @param hop_count_out  Output count of actual hops returned
 * @return               1 if path found, 0 if incomplete or not found
 */
int rpl_route_build_path(const linkaddr_t *dest,
                         linkaddr_t *hops_out,
                         uint8_t max_hops,
                         uint8_t *hop_count_out);

/**
 * @brief Print the routing table to console
 *
 * Debug function to display all routing entries.
 */
void rpl_route_print(void);

/**
 * @brief Generate GUI topology output
 *
 * Produces a formatted output suitable for visualization by the
 * GUI topology viewer showing nodes and their relationships.
 */
void rpl_route_print_gui(void);

/**
 * @brief Initialize node role from link-layer address
 *
 * Looks up the link-layer address in the role mapping table
 * and sets the node's role accordingly.
 *
 * @param a  The link-layer address to look up
 */
void role_init_from_lladdr(const linkaddr_t *a);
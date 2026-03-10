/**
 * @file dis.c
 * @brief Implementation of DIS message handling for RPL
 */

#include "dis.h"
#include "rpl_net.h"
#include "dio.h"
#include "rpl_config.h"
#include "rpl_route.h"
#include "rpl_state.h"

#include "net/linkaddr.h"
#include <string.h>
#include <inttypes.h>

/**
 * @brief Get current DODAG version (weak function)
 *
 * Default implementation returns version 1. May be overridden
 * by application-specific version tracking.
 *
 * @return The current DODAG version number
 */
__attribute__((weak)) uint8_t rpl_state_get_version(void)
{
  return 1;
}

/**
 * @brief Get current node rank (weak function)
 *
 * Default implementation returns 0 for root, 0xFFFF for others.
 * May be overridden for application-specific rank management.
 *
 * @return The node's current rank in the DODAG
 */
__attribute__((weak)) uint16_t rpl_state_get_rank(void)
{
  if (MY_ROLE == ROLE_ROOT)
    return 0;
  return 0xFFFF;
}

/**
 * @brief Get current parent's link-layer address (weak function)
 *
 * Default implementation returns null address. May be overridden
 * for application-specific parent tracking.
 *
 * @return Pointer to parent's link-layer address
 */
__attribute__((weak)) const linkaddr_t *rpl_state_get_parent_lladdr(void)
{
  static linkaddr_t parent_lladdr;
  linkaddr_copy(&parent_lladdr, &linkaddr_null);
  return &parent_lladdr;
}

/**
 * @brief Send a DIS message
 *
 * Constructs and broadcasts a DIS message containing the sender's
 * link-layer address in colon-separated hexadecimal format.
 *
 * @param src_id The sender's link-layer address
 */
void dis_send(const linkaddr_t *src_id)
{
  char msg[80];

  int n = snprintf(msg, sizeof(msg), "DIS ");
  for (int i = 0; i < LINKADDR_SIZE; i++)
  {
    n += snprintf(msg + n, sizeof(msg) - (size_t)n, "%02x%s",
                  src_id->u8[i],
                  (i < LINKADDR_SIZE - 1) ? ":" : "");
    if (n <= 0 || (size_t)n >= sizeof(msg))
    {
      break;
    }
  }

  rpl_broadcast(msg);
}

/**
 * @brief Parse a DIS message to extract requester address
 *
 * Extracts the sender's link-layer address from a DIS message string.
 * Expected format: "DIS f4:ce:36:7a:82:93:ea:cd"
 *
 * @param buf           Pointer to the message buffer
 * @param out_src_id    Output buffer for the parsed address
 * @return              1 if parse successful, 0 on error
 */
static int parse_dis(const char *buf, linkaddr_t *out_src_id)
{
  if (buf == NULL || out_src_id == NULL)
    return 0;
  if (strncmp(buf, "DIS ", 4) != 0)
    return 0;

  const char *p = buf + 4;

  for (int i = 0; i < LINKADDR_SIZE; i++)
  {
    unsigned v;

    if (sscanf(p, "%2x", &v) != 1)
      return 0;
    if (v > 0xFF)
      return 0;

    out_src_id->u8[i] = (uint8_t)v;

    if (i < LINKADDR_SIZE - 1)
    {
      p = strchr(p, ':');
      if (p == NULL)
        return 0;
      p++;
    }
  }
  return 1;
}

/**
 * @brief Handle received DIS message
 *
 * When a DIS is received:
 * - Root and nodes with valid rank respond with a DIO
 * - Access points forward the DIS to their parent via TRICKLE message
 *
 * @param buf       Received message buffer
 * @param len       Message length (unused)
 * @param src       Sender's link-layer address
 * @return          1 if message was handled, 0 otherwise
 */
int dis_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src)
{
  (void)len;
  (void)src;

  linkaddr_t requester_id;
  if (!parse_dis(buf, &requester_id))
    return 0;

  if(MY_ROLE==ROLE_ROOT)
  {
    rpl_state_set_version(rpl_state_get_version() + 1);
  }

  uint16_t my_rank = rpl_state_get_rank();
  uint8_t my_ver = rpl_state_get_version();
  const linkaddr_t *my_par = rpl_state_get_parent_lladdr();

  if (my_rank != 0xFFFF)
  {
    dio_send(/*src_id=*/&linkaddr_node_addr,
             /*ver=*/my_ver,
             /*rank=*/my_rank,
             /*parent_lladdr=*/my_par);
  }

  if (MY_ROLE == ROLE_AP)
  {
    const linkaddr_t *my_parent = rpl_state_get_parent_lladdr();
    if (my_parent->u8[LINKADDR_SIZE - 1] != 0)
    {
      char msg[80];
      snprintf(msg, sizeof(msg), "TRICKLE ");
      rpl_unicast_addr(msg, strlen(msg), my_parent);
      return 1;
    }
    return 0;
  }

  return 1;
}

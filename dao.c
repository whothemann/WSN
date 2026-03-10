/**
 * @file dao.c
 * @brief Implementation of DAO message handling for RPL
 */

#include "dao.h"
#include "rpl_net.h"
#include "rpl_state.h"
#include "rpl_route.h"
#include "rpl_config.h"
#include "rpl_msg.h"

#include "net/linkaddr.h"
#include <string.h>
#include <inttypes.h>

/**
 * @brief Convert link-layer address to hexadecimal string representation
 *
 * Formats a link-layer address as a colon-separated hexadecimal string.
 * Example: "f4:ce:36:7a:82:93:ea:cd"
 *
 * @param out       Output buffer for the string
 * @param out_len   Size of the output buffer
 * @param a         Pointer to the link-layer address to convert
 */
static void lladdr_to_str(char *out, size_t out_len, const linkaddr_t *a)
{
  if (!out || out_len == 0 || !a)
    return;

  size_t p = 0;
  out[0] = '\0';

  for (int i = 0; i < LINKADDR_SIZE; i++)
  {
    int n = snprintf(out + p, out_len - p,
                     (i < LINKADDR_SIZE - 1) ? "%02x:" : "%02x",
                     a->u8[i]);
    if (n < 0 || (size_t)n >= out_len - p)
    {
      out[0] = '\0';
      return;
    }
    p += (size_t)n;
  }
}

/**
 * @brief Parse link-layer address from a string following a prefix
 *
 * Extracts a link-layer address from a string by searching for a
 * prefix and then parsing the hexadecimal bytes that follow.
 * Example: "target:f4:ce:36:7a:82:93:ea:cd"
 *
 * @param p         Pointer to the string to parse
 * @param prefix    Prefix string to search for (e.g., "target:")
 * @param out       Output buffer for the parsed address
 * @return          1 if parsing succeeded, 0 on error
 */
static int parse_lladdr_after_prefix(const char *p,
                                     const char *prefix,
                                     linkaddr_t *out)
{
  if (!p || !prefix || !out)
    return 0;

  const char *s = strstr(p, prefix);
  if (!s)
    return 0;

  s += strlen(prefix);

  for (int i = 0; i < LINKADDR_SIZE; i++)
  {
    unsigned v;
    if (sscanf(s, "%2x", &v) != 1 || v > 0xFF)
      return 0;

    out->u8[i] = (uint8_t)v;

    if (i < LINKADDR_SIZE - 1)
    {
      const char *c = strchr(s, ':');
      if (!c)
        return 0;
      s = c + 1;
    }
  }
  return 1;
}

/**
 * @brief Send DAO message to parent
 *
 * Creates and sends a DAO message containing target and parent addresses.
 * The message is sent as a unicast to the specified destination.
 *
 * @param target    Target node address
 * @param parent    Parent node address
 * @param seq       Sequence number
 * @param to_ll     Destination address
 */
void dao_send_target_parent(const linkaddr_t *target,
                            const linkaddr_t *parent,
                            uint8_t seq,
                            const linkaddr_t *to_ll)
{
  if (!target || !parent || !to_ll)
    return;

  char t_str[3 * LINKADDR_SIZE + 1];
  char p_str[3 * LINKADDR_SIZE + 1];

  lladdr_to_str(t_str, sizeof(t_str), target);
  lladdr_to_str(p_str, sizeof(p_str), parent);

  char msg[160];
  snprintf(msg, sizeof(msg),
           "DAO target:%s parent:%s seq:%u",
           t_str, p_str, (unsigned)seq);

  rpl_unicast_addr(msg, strlen(msg), to_ll);
}

/**
 * @brief Parse and handle received DAO message
 *
 * Extracts target and parent information from DAO message.
 * At the root node, stores the parent relationship in routing table.
 * At non-root nodes, forwards the DAO upwards.
 *
 * @param buf       Received message buffer
 * @param len       Message length
 * @param src_ll    Sender's link-layer address
 * @return          1 if handled successfully, 0 otherwise
 */
int dao_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src_ll)
{
  (void)src_ll;

  if (!buf || len == 0)
    return 0;

  linkaddr_t target = {{0}};
  linkaddr_t parent = {{0}};
  unsigned seq_u = 0;

  if (!parse_lladdr_after_prefix(buf, "target:", &target))
    return 0;

  if (!parse_lladdr_after_prefix(buf, " parent:", &parent))
  {
    if (!parse_lladdr_after_prefix(buf, "parent:", &parent))
      return 0;
  }

  const char *p_seq = strstr(buf, " seq:");
  if (!p_seq)
    p_seq = strstr(buf, "seq:");

  if (!p_seq || sscanf(p_seq, "%*[^0-9]%u", &seq_u) != 1 || seq_u > 255)
    return 0;

  uint8_t seq = (uint8_t)seq_u;
  int trickle = 0;

  if (MY_ROLE == ROLE_ROOT)
  {
    trickle = rpl_route_add_parent(&target, &parent);
    return trickle;
  }

  const linkaddr_t *my_parent = rpl_state_get_parent_lladdr();
  if (my_parent)
  {
    dao_send_target_parent(&target, &parent, seq, my_parent);
  }

  return 0;
}

/**
 * @file rpl_msg.c
 * @brief Implementation of message classification for RPL
 */

#include "rpl_msg.h"
#include <string.h>

/**
 * @brief Classify incoming message by examining its prefix
 *
 * Checks the message buffer for known protocol prefixes to determine
 * the message type. Messages are identified by their text format.
 *
 * @param buf   Pointer to the message buffer (may be NULL)
 * @param len   Length of the message (unused)
 * @return      The message type, or MSG_UNKNOWN if not recognized
 */
rpl_msg_type_t
rpl_msg_classify(const char *buf, uint16_t len)
{
  (void)len;
  if (buf == NULL)
    return MSG_UNKNOWN;

  if (strncmp(buf, "DIO ", 4) == 0)
    return MSG_DIO;
  if (strncmp(buf, "DIS ", 4) == 0)
    return MSG_DIS;
  if (strncmp(buf, "DAO ", 4) == 0)
    return MSG_DAO;
  if (strncmp(buf, "TRICKLE ", 8) == 0)
    return MSG_TRICKLE;
  if (strncmp(buf, "MSG_UP ", 7) == 0)
    return MSG_UP;
  if (strncmp(buf, "MSG_UP_ACK ", 11) == 0)
    return MSG_UP_ACK;
  if (strncmp(buf, "MSG_DOWN ", 9) == 0)
    return MSG_DOWN;
  if (strncmp(buf, "MSG_DOWN_ACK ", 13) == 0)
    return MSG_DOWN_ACK;

  return MSG_UNKNOWN;
}

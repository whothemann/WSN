/**
 * @file rpl_msg.h
 * @brief Message classification and dispatch for RPL protocol
 *
 * This module provides message type classification and handler
 * registration for different RPL message types (DIO, DIS, DAO, etc.).
 */

#pragma once
#include <stdint.h>
#include "net/linkaddr.h"

/**
 * @brief RPL message types
 */
typedef enum
{
  MSG_UNKNOWN = 0, /**< Unknown message type */
  MSG_DIO,         /**< DODAG Information Object */
  MSG_DIS,         /**< DODAG Information Solicitation */
  MSG_DAO,         /**< DODAG Advertisement Object */
  MSG_TRICKLE,     /**< Trickle timer control message */
  MSG_UP,          /**< Upward routing message */
  MSG_UP_ACK,      /**< Upward message acknowledgment */
  MSG_DOWN,        /**< Downward routing message */
  MSG_DOWN_ACK,    /**< Downward message acknowledgment */
} rpl_msg_type_t;

/**
 * @brief Classify a received message by its type
 *
 * Examines the message content to determine its type based on
 * protocol-specific prefixes.
 *
 * @param buf   Pointer to the message buffer
 * @param len   Length of the message
 * @return      The classified message type
 */
rpl_msg_type_t rpl_msg_classify(const char *buf, uint16_t len);

/**
 * @brief Handle received DIO message
 *
 * @param buf   Message buffer
 * @param len   Message length
 * @param src   Sender's link-layer address
 * @return      1 if handled, 0 otherwise
 */
int dio_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src);

/**
 * @brief Handle received DIS message
 *
 * @param buf   Message buffer
 * @param len   Message length
 * @param src   Sender's link-layer address
 * @return      1 if handled, 0 otherwise
 */
int dis_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src);

/**
 * @brief Handle received DAO message
 *
 * @param buf   Message buffer
 * @param len   Message length
 * @param src   Sender's link-layer address
 * @return      1 if handled, 0 otherwise
 */
int dao_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src);

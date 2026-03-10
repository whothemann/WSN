/**
 * @file rpl_config.h
 * @brief RPL protocol configuration constants
 *
 * This file contains all configurable parameters for the RPL
 * routing protocol including timing, parent selection metrics,
 * and role definitions.
 */

#pragma once

#include "contiki.h"

/**
 * @defgroup rpl_config_roles Node Role Definitions
 * @{
 */
#define ROLE_ROOT 1  /**< Root/gateway node (top of DODAG) */
#define ROLE_AP 2    /**< Access point (relay node) */
#define ROLE_NODE 3  /**< Regular bed node */
#define ROLE_NURSE 4 /**< Nurse station */
/** @} */

/**
 * @defgroup rpl_config_timing Timing Configuration
 * @{
 */
#define DIO_TRICKLE_IMIN (0.5 * CLOCK_SECOND)      /**< Initial DIO interval */
#define DIO_TRICKLE_DOUBLINGS 4                    /**< DIO exponential backoff doublings */
#define DIS_PERIOD (10 * CLOCK_SECOND)             /**< DIS broadcast period for disconnected nodes */
#define DAO_PERIOD (2 * CLOCK_SECOND)              /**< DAO message period */
#define PRUNE_PERIOD (5 * CLOCK_SECOND)            /**< Route table pruning period */
#define MSG_UP_RETRY_INTERVAL (2 * CLOCK_SECOND)   /**< Upward message retry period */
#define MSG_UP_ACK_TIMEOUT (3 * CLOCK_SECOND)      /**< Upward message ACK timeout */
#define MSG_DOWN_RETRY_INTERVAL (2 * CLOCK_SECOND) /**< Downward message retry period */
#define MSG_DOWN_ACK_TIMEOUT (3 * CLOCK_SECOND)    /**< Downward message ACK timeout */
#define PARENT_TIMEOUT (16 * CLOCK_SECOND)         /**< Parent candidate timeout */
#define PARENT_CHECK_PER (2 * CLOCK_SECOND)        /**< Parent health check period */
/** @} */

/**
 * @defgroup rpl_config_parent Parent Selection Configuration
 * @{
 */
#define MAX_PARENT_CANDIDATES 2 /**< Maximum candidate parents to track */
#define RSSI_INIT -127          /**< Initial RSSI for new candidates */
#define RSSI_HYST_NODE 10       /**< RSSI hysteresis for bed nodes (dB) */
#define RSSI_HYST_NURSE 4       /**< RSSI hysteresis for nurse (dB) */
/** @} */

/**
 * @defgroup rpl_config_scoring Composite Score Configuration
 *
 * Parent selection uses a composite score combining rank and RSSI metrics,
 * normalized to 0-100 and weighted to prioritize RSSI signal quality.
 * @{
 */
#define RANK_WEIGHT 5         /**< Rank metric weight (15%) */
#define RSSI_WEIGHT 95         /**< RSSI metric weight (85%) */
#define RANK_MAX 5             /**< Maximum rank in network */
#define RSSI_MIN_DBM (-120)    /**< RSSI floor for normalization (dBm) */
#define RSSI_MAX_DBM 0         /**< RSSI ceiling for normalization (dBm) */
/** @} */

/**
 * @defgroup rpl_config_radio Radio Configuration
 * @{
 */
#define RF_CHANNEL 26 /**< IEEE 802.15.4 channel (1-26) */
#define RF_TXPOWER 0  /**< Transmit power (dBm, -40 to +8) */
/** @} */

/**
 * @brief Get RSSI hysteresis value for current node role
 *
 * Nurse nodes use lower hysteresis to more quickly adapt to
 * signal strength changes in the mobility-heavy environment.
 */
#define RSSI_HYST_FOR_ROLE() ((MY_ROLE == ROLE_NURSE) ? RSSI_HYST_NURSE : RSSI_HYST_NODE)
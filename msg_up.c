/**
 * @file msg_up.c
 * @brief Implementation of upward message routing from nodes to root
 *
 * This module handles:
 * - Sending MSG_UP messages (emergencies, heartbeats) from nodes to the root
 * - Acknowledgment (ACK) management with retry logic for reliability
 * - Queuing of pending messages when parent is unavailable
 * - Emergency table tracking at the root for all active emergencies
 * - Path-based forwarding of ACKs back to originating nodes
 *
 * @author WSN Lab Group 6
 * @date 2026
 */

#include "msg_up.h"

#include "rpl_net.h"
#include "rpl_state.h"
#include "rpl_route.h"
#include "rpl_config.h"
#include "rpl_msg.h"
#include "msg_down.h"

#include "contiki.h"
#include "sys/etimer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @defgroup msg_up_constants MSG_UP Configuration Constants
 * @{
 */
#define MSG_UP_MAX_PENDING 4   /**< Maximum number of pending messages in queue */
#define MSG_UP_MAX_MSG_LEN 128 /**< Maximum length of a MSG_UP message */
/** @} */

/**
 * @brief Message transmission retry process
 *
 * Manages retransmission of queued upward messages with
 * backoff strategy for reliable delivery.
 */
PROCESS(msg_up_retry_process, "MSG_UP retry");
static uint8_t retry_process_started; /**< Flag indicating if retry process has been started */

/**
 * @defgroup msg_up_ack_constants MSG_UP ACK Configuration
 * @{
 */
#define MSG_UP_ACK_MAX_LEN 256   /**< Maximum length of an ACK message */
#define MSG_UP_ACK_MAX_HOPS 16   /**< Maximum number of hops in ACK path */
#define MSG_UP_ACK_MAX_RETRIES 20 /**< Maximum retry attempts for ACK timeout */
/** @} */

/**
 * @defgroup msg_up_ack_state ACK Tracking State Variables
 * @{
 */
static uint8_t up_waiting_for_ack;           /**< Node is waiting for ACK from root */
static uint8_t up_ack_retries_left;          /**< Remaining retry attempts */
static uint8_t up_ack_timer_reset;           /**< Flag to reset ACK timer */
static uint8_t up_ack_timer_stop;            /**< Flag to stop ACK timer */
static char up_last_msg[MSG_UP_MAX_MSG_LEN]; /**< Last sent message (for retry) */
static uint16_t up_last_len;                 /**< Length of last sent message */
/** @} */

/**
 * @defgroup msg_up_helpers Helper Functions
 * @brief Utility functions for message parsing and address manipulation
 * @{
 */

/**
 * @brief Convert a link-layer address to hexadecimal string representation
 *
 * @param out Output buffer for the string
 * @param out_len Size of the output buffer
 * @param a Pointer to the link address to convert
 */
static void lladdr_to_str(char *out, size_t out_len, const linkaddr_t *a)
{
    if (!out || !a || out_len == 0)
        return;
    out[0] = '\0';
    size_t used = 0;
    for (size_t i = 0; i < LINKADDR_SIZE; i++)
    {
        if (used + 2 >= out_len)
            break;
        int n = snprintf(out + used, out_len - used, "%02x", a->u8[i]);
        if (n < 0)
        {
            out[0] = '\0';
            return;
        }
        used += (size_t)n;
    }
}

/**
 * @brief Parse a hexadecimal byte from a string
 *
 * @param p Pointer to the start of the hex string (must be at least 2 characters)
 * @param out Output buffer for the parsed byte value
 * @return 1 if parsing was successful, 0 otherwise
 */
static int parse_hex_byte(const char *p, uint8_t *out)
{
    char tmp[3] = {p[0], p[1], '\0'};
    char *end = NULL;
    long v = strtol(tmp, &end, 16);
    if (end == tmp || v < 0 || v > 255)
        return 0;
    *out = (uint8_t)v;
    return 1;
}

/**
 * @brief Parse a link-layer address from hexadecimal string representation
 *
 * @param s Hexadecimal string representation of the address
 * @param out Output buffer for the parsed address
 * @return 1 if parsing was successful, 0 otherwise
 */
static int parse_lladdr(const char *s, linkaddr_t *out)
{
    if (!s || !out)
        return 0;
    if (strlen(s) != LINKADDR_SIZE * 2)
        return 0;
    for (size_t i = 0; i < LINKADDR_SIZE; i++)
    {
        if (!parse_hex_byte(&s[i * 2], &out->u8[i]))
            return 0;
    }
    return 1;
}

/**
 * @brief Find and return the value of a field in a message string
 *
 * Message format: "FIELD1:value1 FIELD2:value2 ..."
 *
 * @param msg The message string to search
 * @param key The field key to find
 * @return Pointer to the field value, or NULL if not found
 */
static const char *find_field(const char *msg, const char *key)
{
    size_t klen = strlen(key);
    const char *p = msg;
    while ((p = strstr(p, key)) != NULL)
    {
        if ((p == msg || p[-1] == ' ') && p[klen] == ':')
            return p + klen + 1;
        p += klen;
    }
    return NULL;
}

/**
 * @brief Copy a field value from the message (up to space or end of string)
 *
 * @param dst Destination buffer
 * @param dst_len Size of destination buffer
 * @param val Source value pointer
 * @return 1 if at least one character was copied, 0 otherwise
 */
static int copy_field_value(char *dst, size_t dst_len, const char *val)
{
    size_t i = 0;
    while (val[i] && val[i] != ' ' && i + 1 < dst_len)
    {
        dst[i] = val[i];
        i++;
    }
    dst[i] = '\0';
    return i > 0;
}

/** @} */

/**
 * @brief Mark a message as sent and start waiting for ACK with retry logic
 *
 * Stores the message for potential retransmission if ACK is not received
 * within the timeout period.
 *
 * @param msg The message that was sent
 * @param len Length of the message
 */
static void node_note_sent_wait_for_ack(const char *msg, uint16_t len)
{
    if (!msg || len == 0)
        return;

    if (!retry_process_started)
    {
        process_start(&msg_up_retry_process, NULL);
        retry_process_started = 1;
    }

    if (len >= sizeof(up_last_msg))
        len = sizeof(up_last_msg) - 1;
    memcpy(up_last_msg, msg, len);
    up_last_msg[len] = '\0';
    up_last_len = len;

    up_waiting_for_ack = 1;
    up_ack_retries_left = MSG_UP_ACK_MAX_RETRIES;
    up_ack_timer_reset = 1;
    up_ack_timer_stop = 0;

    process_poll(&msg_up_retry_process);
}

/**
 * @brief Clear the ACK waiting state after successful acknowledgment
 */
static void node_clear_wait_for_ack(void)
{
    up_waiting_for_ack = 0;
    up_ack_retries_left = 0;
    up_ack_timer_reset = 0;
    up_ack_timer_stop = 1;
    up_last_len = 0;
    up_last_msg[0] = '\0';
}

/**
 * @brief Send an acknowledgment message back to the originating node
 *
 * Constructs a MSG_UP_ACK message with the complete path and sends it
 * via the first hop to reach the destination node.
 *
 * @param label The label of the destination node
 */
static void root_send_msg_up_ack(const char *label)
{
    if (!label || !label[0])
        return;

    linkaddr_t dest;
    linkaddr_t hops[MSG_UP_ACK_MAX_HOPS];
    uint8_t hop_count = 0;

    /* Lookup destination by label (routing table) */
    if (!rpl_route_get_addr_by_label(label, &dest))
    {
        return;
    }

    /* Build the path from root to destination */
    if (!rpl_route_build_path(&dest, hops, MSG_UP_ACK_MAX_HOPS, &hop_count) || hop_count == 0)
    {
        return;
    }

    char hops_str[MSG_UP_ACK_MAX_LEN];
    hops_str[0] = '\0';

    for (uint8_t i = 0; i < hop_count; i++)
    {
        char a_str[3 * LINKADDR_SIZE + 1];
        lladdr_to_str(a_str, sizeof(a_str), &hops[i]);

        size_t used = strlen(hops_str);
        size_t need = strlen(a_str) + (i ? 1 : 0);
        if (used + need + 1 >= sizeof(hops_str))
            break;

        if (i)
            strncat(hops_str, "|", sizeof(hops_str) - used - 1);
        strncat(hops_str, a_str, sizeof(hops_str) - strlen(hops_str) - 1);
    }

    char ack[MSG_UP_ACK_MAX_LEN];
    int n = snprintf(ack, sizeof(ack),
                     "MSG_UP_ACK label:%.31s hops:%s",
                     label, hops_str);

    if (n < 0 || (size_t)n >= sizeof(ack))
        return;

    /* Downwards: send to first hop */
    rpl_unicast_addr(ack, (uint16_t)strlen(ack), &hops[0]);
}

/* Forward declaration */
static void emergency_print_gui(void);

/**
 * @brief Entry in the pending message queue
 */
typedef struct
{
    char msg[MSG_UP_MAX_MSG_LEN]; /**< The message content */
    uint16_t len;                 /**< Message length */
} pending_entry_t;

/**
 * @defgroup msg_up_queue Pending Message Queue
 * @{
 */
static pending_entry_t pending_q[MSG_UP_MAX_PENDING]; /**< Queue buffer */
static uint8_t pending_head = 0;                      /**< Head pointer */
static uint8_t pending_tail = 0;                      /**< Tail pointer */
static uint8_t pending_count = 0;                     /**< Number of pending messages */
/** @} */

/**
 * @defgroup msg_up_emergency Emergency Tracking
 * @{
 */
#define MAX_EMERGENCY_ENTRIES 4 /**< Maximum number of active emergency entries */

/**
 * @brief Emergency table entry tracking active emergencies
 */
typedef struct
{
    char label[32];     /**< Node label with emergency */
    msg_up_type_t type; /**< Emergency type/priority */
} emergency_entry_t;

static emergency_entry_t emergency_table[MAX_EMERGENCY_ENTRIES]; /**< Emergency tracking table */
static uint8_t emergency_count = 0;                              /**< Number of active emergencies */
/** @} */

/**
 * @defgroup msg_up_queue_ops Queue Operations
 * @{
 */

/**
 * @brief Push a message onto the pending queue
 *
 * @param buf The message buffer to queue
 * @param len Length of the message
 * @param why Debug reason for queueing (for logging)
 */
static void pending_push(const char *buf, uint16_t len, const char *why)
{
    (void)why;

    if (!buf || len == 0)
    {
        return;
    }
    if (pending_count >= MSG_UP_MAX_PENDING)
    {
        /* queue full -> drop */
        return;
    }

    pending_entry_t *e = &pending_q[pending_tail];
    if (len >= MSG_UP_MAX_MSG_LEN)
    {
        len = MSG_UP_MAX_MSG_LEN - 1;
    }
    memcpy(e->msg, buf, len);
    e->msg[len] = '\0';
    e->len = len;

    pending_tail = (pending_tail + 1) % MSG_UP_MAX_PENDING;
    pending_count++;

    /* Wake the retry process */
    if (!retry_process_started)
    {
        process_start(&msg_up_retry_process, NULL);
        retry_process_started = 1;
    }
    else
    {
        process_poll(&msg_up_retry_process);
    }
}

/**
 * @brief Peek at the first message in the pending queue without removing it
 *
 * @return Pointer to the first pending entry, or NULL if queue is empty
 */
static pending_entry_t *pending_peek(void)
{
    if (pending_count == 0)
    {
        return NULL;
    }
    return &pending_q[pending_head];
}

/**
 * @brief Remove the first message from the pending queue
 */
static void pending_pop(void)
{
    if (pending_count == 0)
    {
        return;
    }
    pending_head = (pending_head + 1) % MSG_UP_MAX_PENDING;
    pending_count--;
}

/** @} */

/**
 * @defgroup msg_up_emergency_ops Emergency Table Operations
 * @{
 */

/**
 * @brief Find an emergency entry by label
 *
 * @param label The node label to search for
 * @return Index of the entry (0 to emergency_count-1), or -1 if not found
 */
static int find_emergency_entry(const char *label)
{
    for (int i = 0; i < (int)emergency_count; i++)
    {
        if (strcmp(emergency_table[i].label, label) == 0)
        {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Insert a new emergency entry, maintaining priority order
 *
 * Higher priority (lower type value) entries appear first.
 *
 * @param label The node label
 * @param type The emergency type/priority
 */
static void insert_emergency_entry(const char *label, msg_up_type_t type)
{
    if (emergency_count >= MAX_EMERGENCY_ENTRIES)
    {
        /* Table full -> ignore */
        return;
    }

    int insert_pos = (int)emergency_count;

    /* Insert AFTER all entries with lower or equal priority (FIFO for same prio) */
    for (int i = 0; i < (int)emergency_count; i++)
    {
        if (emergency_table[i].type > type)
        {
            insert_pos = i;
            break;
        }
    }

    /* Shift existing entries to make room */
    for (int i = (int)emergency_count; i > insert_pos; i--)
    {
        emergency_table[i] = emergency_table[i - 1];
    }

    /* Insert new entry */
    strncpy(emergency_table[insert_pos].label, label, 31);
    emergency_table[insert_pos].label[31] = '\0';
    emergency_table[insert_pos].type = type;

    emergency_count++;
}

/**
 * @brief Remove an emergency entry by label
 *
 * @param label The node label to remove
 */
static void remove_emergency_by_label(const char *label)
{
    for (int i = 0; i < (int)emergency_count; i++)
    {
        if (strcmp(emergency_table[i].label, label) == 0)
        {
            /* Shift remaining entries left */
            for (int j = i; j < (int)emergency_count - 1; j++)
            {
                emergency_table[j] = emergency_table[j + 1];
            }
            emergency_count--;
            return;
        }
    }
}

/**
 * @brief Update the emergency table with new emergency information
 *
 * Handles three cases:
 * - Nurse resolves an emergency: remove from table
 * - Higher priority emergency from same node: reposition in table
 * - New emergency: insert with appropriate priority
 *
 * @param label The node label
 * @param type The emergency type/priority
 */
static void update_emergency_table(const char *label, msg_up_type_t type)
{
    int idx = find_emergency_entry(label);

    /* Nurse resolves emergency -> remove from tracking */
    if (type == MSG_UP_NURSE)
    {
        if (idx >= 0)
        {
            remove_emergency_by_label(label);
        }
        return;
    }

    /* Emergency from node already in table */
    if (idx >= 0)
    {
        msg_up_type_t old_type = emergency_table[idx].type;

        /* Higher priority -> reposition to reflect new priority */
        if (type < old_type)
        {
            remove_emergency_by_label(label);
            insert_emergency_entry(label, type);
        }
        /* Same or lower priority -> ignore */
        return;
    }

    /* New emergency from new node -> insert with priority */
    insert_emergency_entry(label, type);
}

/** @} */

/**
 * @brief Attempt to send the next pending message
 *
 * Sends the first queued message if a parent route is available.
 *
 * @return 1 if a message was sent, 0 otherwise (no pending message or no parent)
 */
static int try_send_pending(void)
{
    if (pending_count == 0)
    {
        return 0;
    }

    const linkaddr_t *my_parent = rpl_state_get_parent_lladdr();
    if (!my_parent || my_parent->u8[LINKADDR_SIZE - 1] == 0)
    {
        /* No parent available */
        return 0;
    }

    pending_entry_t *pe = pending_peek();
    if (!pe)
    {
        return 0;
    }

    /* Send the message and remove from queue */
    rpl_unicast_addr(pe->msg, pe->len, my_parent);
    pending_pop();
    return 1;
}

/**
 * @brief Retry process thread for message transmission and ACK handling
 *
 * Manages:
 * - ACK timeout and retry logic for messages awaiting acknowledgment
 * - Retransmission of pending messages when parent becomes available
 * - Backoff timing between retry attempts
 */
PROCESS_THREAD(msg_up_retry_process, ev, data)
{
    static struct etimer retry_timer; /**< Timer for retrying pending messages */
    static struct etimer ack_timer;   /**< Timer for ACK timeout */

    PROCESS_BEGIN();
    etimer_set(&retry_timer, MSG_UP_RETRY_INTERVAL);
    etimer_stop(&ack_timer);

    while (1)
    {
        PROCESS_WAIT_EVENT();

        /* Handle ACK timer control requests from TX/RX paths */
        if (up_ack_timer_stop)
        {
            etimer_stop(&ack_timer);
            up_ack_timer_stop = 0;
        }
        if (up_ack_timer_reset && up_waiting_for_ack)
        {
            etimer_set(&ack_timer, MSG_UP_ACK_TIMEOUT);
            up_ack_timer_reset = 0;
        }

        /* ACK timeout -> retry sending the last message or queue it */
        if (ev == PROCESS_EVENT_TIMER && data == &ack_timer && up_waiting_for_ack)
        {
            if (up_ack_retries_left-- > 0)
            {
                const linkaddr_t *my_parent = rpl_state_get_parent_lladdr();
                if (my_parent && my_parent->u8[LINKADDR_SIZE - 1] != 0)
                {
                    rpl_unicast_addr(up_last_msg, up_last_len, my_parent);
                }
                else
                {
                    pending_push(up_last_msg, up_last_len, "ack retry no parent");
                }
                /* Restart timeout for next retry */
                up_ack_timer_reset = 1;
                process_poll(&msg_up_retry_process);
            }
            else
            {
                /* Max retries exceeded, give up */
                node_clear_wait_for_ack();
                etimer_stop(&ack_timer);
            }
        }

        /* Process pending message queue */
        if (pending_count != 0)
        {
            if (try_send_pending())
            {
                if (pending_count != 0)
                {
                    /* More messages to send */
                    process_poll(&msg_up_retry_process);
                }
                else
                {
                    /* Queue is empty */
                    etimer_stop(&retry_timer);
                }
            }
            else
            {
                /* Cannot send yet, restart retry timer */
                etimer_set(&retry_timer, MSG_UP_RETRY_INTERVAL);
            }
        }

        /* Retry timer fired with pending messages */
        if (ev == PROCESS_EVENT_TIMER && data == &retry_timer && pending_count != 0)
        {
            process_poll(&msg_up_retry_process);
        }
    }

    PROCESS_END();
}

/**
 * @brief Initialize the MSG_UP module
 *
 * Starts the retry process for handling message retransmission and ACK timeouts.
 */
void msg_up_init(void)
{
    if (!retry_process_started)
    {
        process_start(&msg_up_retry_process, NULL);
        retry_process_started = 1;
    }
}

/**
 * @brief Send an upward emergency message to the root
 *
 * Sends an emergency message of the specified type. If no parent is available,
 * the message is queued for later transmission.
 *
 * @param type The emergency type (e.g., MSG_UP_CRITICAL, MSG_UP_WARNING)
 */
void msg_up_send(msg_up_type_t type)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "MSG_UP %d %s", (int)type, MY_LABEL);

    const linkaddr_t *my_parent = rpl_state_get_parent_lladdr();
    if (!my_parent || my_parent->u8[LINKADDR_SIZE - 1] == 0)
    {
        /* Queue the message if no parent available */
        pending_push(msg, (uint16_t)strlen(msg), "no parent");
        return;
    }

    /* Send to parent */
    rpl_unicast_addr(msg, (uint16_t)strlen(msg), my_parent);

    /* Start waiting for ACK with retry logic */
    node_note_sent_wait_for_ack(msg, (uint16_t)strlen(msg));
    process_poll(&msg_up_retry_process);
}

/**
 * @brief Send a heartbeat message with heart rate information
 *
 * Sends a heartbeat message including the current BPM (beats per minute).
 * The message expects an ACK, allowing the node to retry if the uplink is lost.
 *
 * @param bpm Heart rate in beats per minute
 */
void msg_up_send_heartbeat(uint16_t bpm)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "MSG_UP %d %s BPM:%u", (int)MSG_UP_HEARTBEAT, MY_LABEL, bpm);

    const linkaddr_t *my_parent = rpl_state_get_parent_lladdr();
    if (!my_parent || my_parent->u8[LINKADDR_SIZE - 1] == 0)
    {
        /* Queue the message if no parent available */
        pending_push(msg, (uint16_t)strlen(msg), "no parent");
        return;
    }

    /* Send to parent */
    rpl_unicast_addr(msg, (uint16_t)strlen(msg), my_parent);

    /* Wait for ACK with retry capability */
    node_note_sent_wait_for_ack(msg, (uint16_t)strlen(msg));
    process_poll(&msg_up_retry_process);
}

/**
 * @brief Handle received upward or ACK messages
 *
 * Processes both incoming MSG_UP messages (from nodes) and MSG_UP_ACK messages
 * (from root). For non-root nodes, MSG_UP messages are forwarded to the parent.
 * For the root, MSG_UP messages are parsed and the emergency table is updated.
 *
 * @param buf The received message buffer
 * @param len Length of the received message
 * @param src_ll Source link-layer address
 * @return 1 if message was handled, 0 otherwise
 */
int msg_up_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src_ll)
{
    if (!buf || len == 0 || !src_ll)
    {
        return 0;
    }

    /* ================================================================ */
    /* Handle MSG_UP_ACK (downward path-based forwarding) */
    /* ================================================================ */
    char tmp_ack[MSG_UP_ACK_MAX_LEN];
    uint16_t ack_n = (len < (uint16_t)(sizeof(tmp_ack) - 1)) ? len : (uint16_t)(sizeof(tmp_ack) - 1);
    memcpy(tmp_ack, buf, ack_n);
    tmp_ack[ack_n] = '\0';

    if (strncmp(tmp_ack, "MSG_UP_ACK", 10) == 0)
    {
        /* Root should never receive MSG_UP_ACK */
        if (MY_ROLE == ROLE_ROOT)
        {
            return 1;
        }

        char label[32] = {0};
        char hops[MSG_UP_ACK_MAX_LEN] = {0};

        /* Parse label and hops from ACK message */
        const char *l = find_field(tmp_ack, "label");
        const char *h = find_field(tmp_ack, "hops");
        if (!l || !h)
            return 1;

        copy_field_value(label, sizeof(label), l);
        copy_field_value(hops, sizeof(hops), h);

        /* hops is pipe-separated list: hop1|hop2|...|hopN */
        /* We drop the first element and forward to the next hop */
        char *sep = strchr(hops, '|');
        if (!sep)
        {
            /* No separator: this is the final hop (destination) */
            if (strcmp(label, MY_LABEL) == 0)
            {
                /* This node is the destination, ACK received successfully */
                node_clear_wait_for_ack();
                process_poll(&msg_up_retry_process);
            }
            return 1;
        }

        char *rest = sep + 1;

        /* Extract next hop token (first item of 'rest') */
        char next_tok[LINKADDR_SIZE * 2 + 1];
        size_t i = 0;
        while (rest[i] && rest[i] != '|' && i + 1 < sizeof(next_tok))
        {
            next_tok[i] = rest[i];
            i++;
        }
        next_tok[i] = '\0';

        linkaddr_t nh;
        if (!parse_lladdr(next_tok, &nh))
            return 1;

        /* Forward with remaining path starting at the next hop */
        char fwd[MSG_UP_ACK_MAX_LEN];
        int fn = snprintf(fwd, sizeof(fwd),
                          "MSG_UP_ACK label:%.31s hops:%s",
                          label, rest);
        if (fn < 0 || (size_t)fn >= sizeof(fwd))
            return 1;

        rpl_unicast_addr(fwd, (uint16_t)strlen(fwd), &nh);
        return 1;
    }

    /* ================================================================ */
    /* Handle MSG_UP messages (non-root nodes forward to parent) */
    /* ================================================================ */

    /* Non-root: forward MSG_UP unchanged to preferred parent */
    if (MY_ROLE != ROLE_ROOT)
    {
        const linkaddr_t *my_parent = rpl_state_get_parent_lladdr();
        if (my_parent && my_parent->u8[LINKADDR_SIZE - 1] != 0)
        {
            rpl_unicast_addr(buf, len, my_parent);
        }
        else
        {
            /* No parent available, queue for later */
            pending_push(buf, len, "forwarding no parent");
        }
        return 1;
    }

    /* ================================================================ */
    /* ROOT: parse and process MSG_UP */
    /* ================================================================ */
    char tmp[128];
    uint16_t n = (len < (uint16_t)(sizeof(tmp) - 1) ? len : (uint16_t)(sizeof(tmp) - 1));
    memcpy(tmp, buf, n);
    tmp[n] = '\0';

    char type_str[16] = {0};
    char label[32] = {0};
    unsigned int bpm_value = 0;

    /* Parse: MSG_UP <type> <label> [BPM:<value>] */
    int fields = sscanf(tmp, "MSG_UP %15s %31s", type_str, label);
    if (fields != 2)
    {
        return 1;
    }

    msg_up_type_t msg_type = (msg_up_type_t)atoi(type_str);

    /* Handle heartbeat messages separately */
    if (msg_type == MSG_UP_HEARTBEAT)
    {
        if (sscanf(tmp, "MSG_UP %*s %*s BPM:%u", &bpm_value) == 1)
        {
            printf("[HEARTBEAT] %s BPM: %u\n", label, bpm_value);
            if (bpm_value < 40 || bpm_value > 180)
            {
                msg_type = MSG_UP_HEARTBEAT_EMERGENCY;
            }
            else

            {
                root_send_msg_up_ack(label);
                /* Normal heartbeat, no emergency */
                return 1;
            }
        }
    }

    /* Update emergency table with new emergency information */
    update_emergency_table(label, msg_type);

    /* Output current emergency state for GUI */
    emergency_print_gui();

    /* Send ACK to the originating node BEFORE triggering any response */
    root_send_msg_up_ack(label);

    /* Send top emergency to nurse if priority has changed */
    static char last_sent_to_nurse[32] = "";
    const char *want = (emergency_count > 0) ? emergency_table[0].label : "EMPTY";
    if (strcmp(last_sent_to_nurse, want) != 0)
    {
        strncpy(last_sent_to_nurse, want, sizeof(last_sent_to_nurse) - 1);
        last_sent_to_nurse[sizeof(last_sent_to_nurse) - 1] = '\0';

        /* Check if the top emergency is a heartbeat emergency */
        int is_heartbeat_emergency = (emergency_count > 0 &&
                                      emergency_table[0].type == MSG_UP_HEARTBEAT_EMERGENCY);
        msg_down_send_to_nurse(want, is_heartbeat_emergency);
    }

    return 1;
}

static int type_to_gui_prio(msg_up_type_t type)
{
    switch(type) {
    case MSG_UP_EMERGENCY:
    case MSG_UP_HEARTBEAT_EMERGENCY:
        return 0; /* red */

    case MSG_UP_TOILET:
        return 1; /* yellow */

    case MSG_UP_WATER:   
        return 2; /* green */

    default:
        return 2; /* fallback: green */
    }
}


/**
 * @brief Output current emergency table state for GUI consumption
 *
 * Prints emergency table in a structured format that the GUI can parse:
 * ```
 * EMERG_BEGIN
 * EMERG <label> <type>
 * EMERG <label> <type>
 * ...
 * EMERG_END
 * ```
 */
static void emergency_print_gui(void)
{
    printf("EMERG_BEGIN\n");
    for (int i = 0; i < (int)emergency_count; i++)
    {
        printf("EMERG %s %d\n", emergency_table[i].label, type_to_gui_prio(emergency_table[i].type));
    }
    printf("EMERG_END\n");
}
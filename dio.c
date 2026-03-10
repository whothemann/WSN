/**
 * @file dio.c
 * @brief Implementation of DIO message handling for RPL parent selection
 */

#include "dio.h"
#include "rpl_config.h"
#include "rpl_net.h"
#include "rpl_msg.h"
#include "rpl_state.h"
#include "rpl_route.h"
#include "dao.h"
#include "net/linkaddr.h"
#include "sys/clock.h"
#include "dis.h"
#include "net/packetbuf.h"
#include <string.h>
#include "dev/leds.h"
#include <inttypes.h>

/**
 * @brief Entry in the candidate parent list
 *
 * Represents one neighbor that has sent a DIO and is a potential parent.
 * Stores metrics used for parent selection.
 */
typedef struct
{
  uint8_t used;            /**< Whether this entry is in use */
  linkaddr_t addr;         /**< Link-layer address of the candidate */
  int16_t avg_rssi;        /**< Average RSSI of received DIOs */
  uint16_t last_rank;      /**< Last advertised rank from this candidate */
  int16_t composite_score; /**< Combined rank and RSSI score (0-100) */
  clock_time_t last_seen;  /**< Timestamp of last received DIO */
} cand_t;

/**
 * @brief List of candidate parent nodes
 */
static cand_t cand[MAX_PARENT_CANDIDATES];

/**
 * @brief Last byte of current parent's address
 *
 * Cached for efficient parent change detection. Value 0 means no parent.
 */
static int8_t parent_last_bytes = 0;

/**
 * @brief Get or create a candidate parent entry
 *
 * Searches for existing candidate by address or creates new entry
 * if space available.
 *
 * @param src_id    Address to search for or create
 * @return          Pointer to candidate entry or NULL if table full
 */
static cand_t *get_cand(const linkaddr_t *src_id)
{
  for (int i = 0; i < MAX_PARENT_CANDIDATES; i++)
  {
    if (cand[i].used && linkaddr_cmp(&cand[i].addr, src_id))
    {
      return &cand[i];
    }
  }

  for (int i = 0; i < MAX_PARENT_CANDIDATES; i++)
  {
    if (!cand[i].used)
    {
      cand[i].used = 1;
      linkaddr_copy(&cand[i].addr, src_id);
      cand[i].avg_rssi = RSSI_INIT;
      cand[i].last_seen = clock_time();
      return &cand[i];
    }
  }
  return NULL;
}

/**
 * @brief Find a candidate parent by address
 *
 * @param src_id The address of the candidate parent to find
 * @return       Pointer to the candidate parent entry, or NULL if not found
 */
static cand_t *find_cand(linkaddr_t *src_id)
{
  for (int i = 0; i < MAX_PARENT_CANDIDATES; i++)
  {
    if (cand[i].used && linkaddr_cmp(&cand[i].addr, src_id))
      return &cand[i];
  }
  return NULL;
}

/**
 * @brief Reset DODAG generation and clear parent candidates
 *
 * Called when a new DODAG version is detected. Clears the candidate
 * list and resets rank information to start fresh.
 *
 * @param ver The new DAG version number
 */
static void reset_generation(uint8_t ver)
{
  rpl_state_set_version(ver);
  for (int i = 0; i < MAX_PARENT_CANDIDATES; i++)
    cand[i].used = 0;

  linkaddr_t null_addr = {.u8 = {0}};
  rpl_state_set_parent_lladdr(&null_addr);
  rpl_state_set_rank(0xFFFF);
}

/**
 * @brief Compute composite parent score from rank and RSSI
 *
 * Normalizes rank and RSSI to 0-100 scales, then combines them
 * using configured weights. Higher score indicates better parent.
 *
 * Rank is normalized as (RANK_MAX - rank) / RANK_MAX * 100
 * RSSI is normalized as (rssi - RSSI_MIN_DBM) / (RSSI_MAX_DBM - RSSI_MIN_DBM) * 100
 *
 * @param rank The advertised rank from the candidate
 * @param rssi The average RSSI value in dBm
 * @return     Composite score (0-100)
 */
static int16_t compute_parent_score(uint16_t rank, int16_t rssi)
{
  int16_t rank_score = 0;
  if (rank <= RANK_MAX)
  {
    rank_score = ((RANK_MAX - rank) * 100) / RANK_MAX;
  }
  if (rank_score < 0)
    rank_score = 0;
  if (rank_score > 100)
    rank_score = 100;

  int16_t rssi_clamped = rssi;
  if (rssi_clamped < RSSI_MIN_DBM)
    rssi_clamped = RSSI_MIN_DBM;
  if (rssi_clamped > RSSI_MAX_DBM)
    rssi_clamped = RSSI_MAX_DBM;

  int16_t rssi_score = ((rssi_clamped - RSSI_MIN_DBM) * 100) /
                       (RSSI_MAX_DBM - RSSI_MIN_DBM);
  if (rssi_score < 0)
    rssi_score = 0;
  if (rssi_score > 100)
    rssi_score = 100;

  int16_t composite = (rank_score * RANK_WEIGHT + rssi_score * RSSI_WEIGHT) / 100;

  return composite;
}

/**
 * @brief Convert link-layer address to hexadecimal string
 *
 * Formats address as colon-separated hex bytes (e.g., "f4:ce:36:7a:82:93:ea:cd")
 *
 * @param out       Output buffer for the string
 * @param out_len   Size of output buffer
 * @param a         Address to convert (may be NULL)
 */
static void lladdr_to_str(char *out, size_t out_len, const linkaddr_t *a)
{
  if (!out || out_len == 0)
    return;

  if (a == NULL)
  {
    snprintf(out, out_len, "NULL");
    return;
  }

  size_t n = 0;
  for (int i = 0; i < LINKADDR_SIZE; i++)
  {
    int wrote = snprintf(out + n, out_len - n, "%02x%s",
                         a->u8[i],
                         (i < LINKADDR_SIZE - 1) ? ":" : "");
    if (wrote < 0)
      break;
    n += (size_t)wrote;
    if (n >= out_len)
      break;
  }
}

/**
 * @brief Parse link-layer address from hexadecimal string
 *
 * Parses colon-separated hex bytes into a link-layer address.
 *
 * @param p       Input string to parse
 * @param out     Output address buffer
 * @return        1 if successful, 0 on parse error
 */
static int parse_lladdr(const char *p, linkaddr_t *out)
{
  if (!p || !out)
    return 0;

  for (int i = 0; i < LINKADDR_SIZE; i++)
  {
    unsigned v;
    if (sscanf(p, "%2x", &v) != 1)
      return 0;
    if (v > 0xFF)
      return 0;
    out->u8[i] = (uint8_t)v;

    if (i < LINKADDR_SIZE - 1)
    {
      const char *c = strchr(p, ':');
      if (!c)
        return 0;
      p = c + 1;
    }
  }
  return 1;
}
/**
 * @brief Build and broadcast a DIO message
 *
 * Creates a DIO message with source address, version, rank, and parent
 * information, then broadcasts it using rpl_broadcast().
 *
 * @param src_id            Sender's link-layer address
 * @param ver               Current DODAG version
 * @param rank              Node's rank in DODAG
 * @param parent_lladdr     Parent's address (NULL or last byte 0 for root)
 */
void dio_send(const linkaddr_t *src_id, uint8_t ver, uint16_t rank,
              const linkaddr_t *parent_lladdr)
{
  if(MY_ROLE == ROLE_ROOT){
    leds_single_toggle(LEDS_LED1);
  }
  char msg[220];
  char src_str[3 * LINKADDR_SIZE + 1];
  char parent_str[3 * LINKADDR_SIZE + 1];

  lladdr_to_str(src_str, sizeof(src_str), src_id);

  uint8_t parent_last = (parent_lladdr != NULL) ? parent_lladdr->u8[LINKADDR_SIZE - 1] : 0;

  if (parent_lladdr == NULL || parent_last == 0)
  {
    snprintf(msg, sizeof(msg),
             "DIO src:%s ver:%u rank:%u parent:ROOT",
             src_str, (unsigned)ver, (unsigned)rank);
  }
  else
  {
    lladdr_to_str(parent_str, sizeof(parent_str), parent_lladdr);
    snprintf(msg, sizeof(msg),
             "DIO src:%s ver:%u rank:%u parent:%s",
             src_str, (unsigned)ver, (unsigned)rank, parent_str);
  }

  rpl_broadcast(msg);
}

/**
 * @brief Handle received DIO message
 *
 * Parses DIO message, updates candidate parent list, and performs parent
 * selection based on composite score (rank + RSSI) with hysteresis.
 * Access points relay the DIO to other nodes.
 *
 * @param buf       Received message buffer
 * @param len       Message length
 * @param src       Sender's link-layer address
 * @return          1 if message was processed, 0 on error
 */
int dio_handle_rx(const char *buf, uint16_t len, const linkaddr_t *src)
{
  (void)src;

  if (!buf || len == 0)
    return 0;

  /* ROOT ignores DIOs */
  if (MY_ROLE == ROLE_ROOT)
  {
    return 1;
  }

  printf("[DIO RX] %.*s\n", len, buf);
  
  linkaddr_t dio_src = {{0}};
  uint16_t dio_ver = 0, dio_rank = 0;

  /* Find the "src:" field */
  const char *p_src = strstr(buf, "DIO src:");
  if (!p_src)
    return 0;
  p_src += strlen("DIO src:");

  if (!parse_lladdr(p_src, &dio_src))
    return 0;

  /* Parse ver and rank */
  const char *p_ver = strstr(buf, " ver:");
  const char *p_rank = strstr(buf, " rank:");
  if (!p_ver || !p_rank)
    return 0;

  if (sscanf(p_ver, " ver:%" SCNu16, &dio_ver) != 1)
    return 0;
  if (sscanf(p_rank, " rank:%" SCNu16, &dio_rank) != 1)
    return 0;

  /* Version / generation handling */
  if (rpl_state_get_version() == 0)
  {
    reset_generation((uint8_t)dio_ver);
  }
  else if (dio_ver < rpl_state_get_version())
  {
    return 1; /* old gen but “handled” */
  }
  else if (dio_ver > rpl_state_get_version())
  {
    reset_generation((uint8_t)dio_ver);
  }

  /* RSSI from packetbuf */
  int16_t rssi = (int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI);

  cand_t *c = get_cand(&dio_src);
  if (!c)
    return 1;

  c->last_seen = clock_time();
  c->last_rank = dio_rank; /* Store the advertised rank */

  /* Update average RSSI */
  
  if (MY_ROLE == ROLE_NURSE) {
    /* Nurses: react fast -> use most recent RSSI only */
    c->avg_rssi = rssi;
  } else {
    /* Others: keep smoothing (EMA) */
    if (c->avg_rssi == RSSI_INIT) {
      c->avg_rssi = rssi;
    } else {
      int shift = 2; /* or keep your role logic here */
      c->avg_rssi = c->avg_rssi + ((rssi - c->avg_rssi) >> shift);
    }
  }
  
  /* Compute composite score for this candidate */
  c->composite_score = compute_parent_score(c->last_rank, c->avg_rssi);

  /* Debug: Show received DIO info */
  char src_str[3 * LINKADDR_SIZE + 1];
  lladdr_to_str(src_str, sizeof(src_str), &dio_src);

  const linkaddr_t *parent_ptr = rpl_state_get_parent_lladdr();
  cand_t *pcur = find_cand((linkaddr_t *)parent_ptr);
  int16_t cur_parent_score = (pcur != NULL) ? pcur->composite_score : -100;

  static uint8_t dao_seq = 0;

  /* Parent selection using composite score with hysteresis */
  if (parent_last_bytes == 0 || c->composite_score > cur_parent_score + RSSI_HYST_FOR_ROLE())
  {
    char new_parent_str[3 * LINKADDR_SIZE + 1];
    lladdr_to_str(new_parent_str, sizeof(new_parent_str), &c->addr);

    rpl_state_set_parent_lladdr(&c->addr);
    rpl_state_set_rank(dio_rank + 1);

    const linkaddr_t *new_parent = rpl_state_get_parent_lladdr();
    parent_last_bytes = new_parent->u8[LINKADDR_SIZE - 1];

    /* Send DAO to new parent */
    if (MY_ROLE != ROLE_ROOT)
    {
      const linkaddr_t *p = rpl_state_get_parent_lladdr();
      dao_send_target_parent(rpl_state_get_lladdr(), p, dao_seq++, p);
    }
  }

  /* AP forwards DIO (acts as relay) */
  if (MY_ROLE == ROLE_AP)
  {
    linkaddr_t my_id = *rpl_state_get_lladdr();
    dio_send(/*src_id=*/&my_id,
             /*ver=*/rpl_state_get_version(),
             /*rank=*/rpl_state_get_rank(),
             /*parent_lladdr=*/rpl_state_get_parent_lladdr());
  }

  return 1;
}

/**
 * @brief Check for timed-out parent candidates
 *
 * Removes candidates not heard from within PARENT_TIMEOUT period and
 * verifies current parent is still reachable. If current parent has
 * timed out, resets rank to unknown state.
 */
void dio_parent_timeout_check(void)
{
  if (MY_ROLE == ROLE_ROOT)
    return;

  clock_time_t now = clock_time();

  for (int i = 0; i < MAX_PARENT_CANDIDATES; i++)
  {
    if (cand[i].used)
    {
      if ((clock_time_t)(now - cand[i].last_seen) > PARENT_TIMEOUT)
      {
        cand[i].used = 0;
      }
    }
  }

  const linkaddr_t *cur_parent = rpl_state_get_parent_lladdr();
  if (cur_parent == NULL)
    return;

  if (cur_parent->u8[LINKADDR_SIZE - 1] == 0)
    return;

  if (find_cand((linkaddr_t *)cur_parent) == NULL)
  {
    linkaddr_t null_addr = {.u8 = {0}};
    rpl_state_set_parent_lladdr(&null_addr);
    rpl_state_set_rank(0xFFFF);
    parent_last_bytes = 0;
  }
}

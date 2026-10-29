// ============================================================================
// ZiGate Console - voir zigate_console.h
// ============================================================================

#include "zigate_console.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "ZIGATE_CON";

typedef struct {
    uint32_t seq;
    char     text[ZIGATE_CONSOLE_LINE_LEN];
} zc_line_t;

static zc_line_t        *s_lines = NULL;   // anneau en PSRAM
static uint32_t          s_last_seq = 0;   // numero de la derniere ligne ecrite
static uint32_t          s_first_seq = 1;  // plus ancienne ligne encore valide
static SemaphoreHandle_t s_lock = NULL;

void zigate_console_init(void)
{
    if (s_lock) return;
    s_lock = xSemaphoreCreateMutex();
    size_t sz = sizeof(zc_line_t) * ZIGATE_CONSOLE_LINES;
    s_lines = (zc_line_t *)heap_caps_calloc(1, sz, MALLOC_CAP_SPIRAM);
    if (!s_lines) {
        // Pas de repli en RAM interne : 40 Ko y manqueraient. La console est
        // alors simplement muette, le pont fonctionne normalement.
        ESP_LOGW(TAG, "PSRAM indisponible : console ZiGate desactivee");
    }
}

/** Horodatage court : heure locale si l'horloge est reglee, sinon uptime. */
static int stamp(char *out, size_t len)
{
    time_t now = time(NULL);
    if (now > 1600000000) {
        struct tm t;
        localtime_r(&now, &t);
        return snprintf(out, len, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    }
    int64_t us = esp_timer_get_time();
    return snprintf(out, len, "+%lu.%03lus", (unsigned long)(us / 1000000),
                    (unsigned long)((us / 1000) % 1000));
}

// Correction (pile) : les lignes etaient formatees dans un tampon local de
// 152 octets puis copiees, sur la pile de la tache emettrice, qui portait deja
// le paquet et le tampon d'encodage de l'envoi. Elles sont desormais formatees
// directement dans leur case de l'anneau (en PSRAM), verrou tenu : aucune
// grande variable locale.

/** Prend le verrou et reserve la case suivante ; NULL si indisponible. */
static zc_line_t *line_begin(void)
{
    if (!s_lines || !s_lock) return NULL;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) return NULL;   // jamais bloquer le pilote
    zc_line_t *l = &s_lines[(s_last_seq + 1) % ZIGATE_CONSOLE_LINES];
    l->text[0] = '\0';
    return l;
}

/** Publie la case reservee par line_begin() et rend le verrou. */
static void line_commit(zc_line_t *l)
{
    s_last_seq++;
    l->text[sizeof(l->text) - 1] = '\0';
    l->seq = s_last_seq;
    if (s_last_seq - s_first_seq >= ZIGATE_CONSOLE_LINES) {
        s_first_seq = s_last_seq - ZIGATE_CONSOLE_LINES + 1;
    }
    xSemaphoreGive(s_lock);
}

/** Libelle des codes de trame les plus frequents, pour une console lisible. */
static const char *cmd_label(uint16_t cmd)
{
    switch (cmd) {
    case 0x0002: return "RawMode";
    case 0x0009: return "GetNetworkState";
    case 0x0010: return "GetVersion";
    case 0x0011: return "Reset";
    case 0x0012: return "ErasePDM";
    case 0x0018: return "SetLed";
    case 0x0021: return "SetChannelMask";
    case 0x0024: return "StartNetwork";
    case 0x0026: return "RemoveDevice";
    case 0x0030: return "Bind";
    case 0x0043: return "SimpleDescReq";
    case 0x0045: return "ActiveEpReq";
    case 0x0049: return "PermitJoin";
    case 0x004C: return "LeaveReq";
    case 0x004D: return "DeviceAnnounce";
    case 0x0092: return "OnOff";
    case 0x00FA: return "WindowCovering";
    case 0x0100: return "ReadAttr";
    case 0x0110: return "WriteAttr";
    case 0x0120: return "ConfigReport";
    case 0x0530: return "RawAPS";
    case 0x0602: return "NwkRecoveryExtract";
    case 0x0603: return "NwkRecoveryRestore";
    case 0x8000: return "Status";
    case 0x8001: return "Log";
    case 0x8002: return "DataIndication";
    case 0x8009: return "NetworkState";
    case 0x8010: return "Version";
    case 0x8011: return "AckData";
    case 0x8012: return "ApsConfirm";
    case 0x8014: return "PermitJoinStatus";
    case 0x8024: return "NetworkStarted";
    case 0x8030: return "BindResp";
    case 0x8042: return "NodeDesc";
    case 0x8043: return "SimpleDesc";
    case 0x8045: return "ActiveEp";
    case 0x8048: return "Leave";
    case 0x8100: return "ReadAttrResp";
    case 0x8101: return "DefaultResp";
    case 0x8102: return "AttrReport";
    case 0x8110: return "WriteAttrResp";
    case 0x8120: return "ConfigReportResp";
    case 0x8602: return "NwkRecoveryData";
    case 0x8701: return "RouteDiscovery";
    case 0x8702: return "ApsFail";
    default:     return "";
    }
}

void zigate_console_frame(char dir, const zigate_packet_t *pkt)
{
    if (!pkt) return;
    zc_line_t *l = line_begin();
    if (!l) return;

    // Ligne : "12:34:56 < 8102 AttrReport [12] lqi 156 : 01 02 ..."
    char *line = l->text;
    const int cap = (int)sizeof(l->text);
    int n = stamp(line, cap);
    if (n < 0 || n >= cap) n = cap - 1;
    const char *lbl = cmd_label(pkt->cmd);
    int r;
    if (dir == '<') {
        r = snprintf(line + n, cap - n, " < %04X %s [%u] lqi %u :", pkt->cmd, lbl,
                     (unsigned)pkt->length, (unsigned)pkt->lqi);
    } else {
        r = snprintf(line + n, cap - n, " > %04X %s [%u] :", pkt->cmd, lbl,
                     (unsigned)pkt->length);
    }
    n = (r < 0 || n + r >= cap) ? cap - 1 : n + r;
    // Octets tant qu'il reste de la place, puis "..." si la trame est plus
    // longue. Conversion hexa a la main : pas d'appel a snprintf par octet.
    static const char HEX[] = "0123456789ABCDEF";
    unsigned i = 0;
    for (; i < pkt->length && i < sizeof(pkt->payload); i++) {
        if (n + 3 + 4 >= cap) break;
        line[n++] = ' ';
        line[n++] = HEX[pkt->payload[i] >> 4];
        line[n++] = HEX[pkt->payload[i] & 0x0F];
        line[n] = '\0';
    }
    if (i < pkt->length && n + 4 < cap) {
        strlcat(line, " ...", cap);
    }
    line_commit(l);
}

void zigate_console_printf(const char *fmt, ...)
{
    zc_line_t *l = line_begin();
    if (!l) return;
    char *line = l->text;
    const int cap = (int)sizeof(l->text);
    int n = stamp(line, cap);
    if (n < 0 || n >= cap) n = cap - 1;
    if (n + 3 < cap) {
        memcpy(line + n, " # ", 4);
        n += 3;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(line + n, cap - n, fmt, ap);
        va_end(ap);
    }
    line_commit(l);
}

void zigate_console_clear(void)
{
    if (!s_lines || !s_lock) return;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;
    s_first_seq = s_last_seq + 1;   // plus rien de lisible, sequence conservee
    xSemaphoreGive(s_lock);
}

uint32_t zigate_console_foreach(uint32_t since, size_t max_lines,
                                zigate_console_cb_t cb, void *ctx)
{
    if (!s_lines || !s_lock) return since;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return since;

    uint32_t start = since + 1;
    if (start < s_first_seq || start > s_last_seq + 1) start = s_first_seq;   // client en retard ou apres un redemarrage
    // Avec une limite, ne rendre que les plus recentes
    if (max_lines && s_last_seq + 1 > start && s_last_seq + 1 - start > max_lines) {
        start = s_last_seq + 1 - (uint32_t)max_lines;
    }
    for (uint32_t s = start; s <= s_last_seq && s != 0; s++) {
        const zc_line_t *l = &s_lines[s % ZIGATE_CONSOLE_LINES];
        if (l->seq == s && cb) cb(s, l->text, ctx);
    }
    uint32_t last = s_last_seq;
    xSemaphoreGive(s_lock);
    return last;
}

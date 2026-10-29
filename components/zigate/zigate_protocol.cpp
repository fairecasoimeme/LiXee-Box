// ============================================================================
// ZiGate Protocol - Encodage/Decodage du protocole serie ZiGate
// ============================================================================

#include "zigate_protocol.h"
#include "zigate_driver.h"
#include "zigate_devices.h"
#include "zigate_console.h"
#include "zigate_actions.h"     // zigate_zcl_type_size()
#include "zigate_ota.h"
#include "zigate_ir.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>

static const char *TAG = "ZIGATE_PROTO";

// Callbacks externes
extern zigate_attribute_cb_t             s_attr_cb;
extern zigate_device_announce_cb_t       s_announce_cb;
extern zigate_device_leave_cb_t          s_leave_cb;
extern zigate_onoff_cmd_cb_t             s_onoff_cmd_cb;
static void emit_zone_status(uint16_t sa, uint8_t ep, uint16_t zs);
extern zigate_device_discovery_done_cb_t s_discovery_done_cb;

// Protection contre la double decouverte
// (le Device Announce arrive souvent 2 fois)
static uint16_t s_discovery_in_progress_addr = 0xFFFF;

// Adresse IEEE du coordinateur (reponse 0x8009). Necessaire pour lier les
// clusters d'un appareil au coordinateur ; jusqu'ici seulement journalisee.
static volatile uint64_t s_coordinator_ieee = 0;

uint64_t zigate_get_coordinator_ieee(void)
{
    return s_coordinator_ieee;
}

// Etat du coordinateur expose a l'interface web (page Zigbee, assistant
// d'appairage). Le pilote (zigate_driver.cpp) y note aussi les commandes
// emises. Champs ecrits sans verrou : une lecture concurrente peut au pire
// melanger deux mises a jour successives, sans consequence pour un affichage.
zigate_coord_info_t g_zigate_coord = {
    .version_known = false, .version = {0, 0, 0, 0},
    .state_known = false, .short_addr = 0xFFFF, .ieee = 0, .pan_id = 0, .ext_pan_id = 0,
    .channel = 0, .start_status = -1, .raw_mode = false, .led = -1,
    .permit_join_until = 0, .join_seq = 0, .last_join_ieee = 0, .last_join_short = 0,
    .last_join_s = 0, .ghost_pending = false, .ghost_result = -1,
    .frames_rx = 0, .frames_tx = 0,
};

void zigate_get_coord_info(zigate_coord_info_t *out)
{
    if (out) *out = g_zigate_coord;
}

bool zigate_is_detected(void)
{
    return g_zigate_coord.frames_rx > 0;
}

// Observateurs des statuts et retours radio (voir zigate_driver.h)
static zigate_status_cb_t s_status_cb = NULL;
static zigate_radio_cb_t  s_radio_cb  = NULL;
void zigate_register_status_cb(zigate_status_cb_t cb) { s_status_cb = cb; }
void zigate_register_radio_cb(zigate_radio_cb_t cb)   { s_radio_cb = cb; }

static zigate_sleepy_cb_t s_sleepy_cb = NULL;
static bool s_fail_quiet;                  // tache de traitement ZiGate seule
void zigate_register_sleepy_cb(zigate_sleepy_cb_t cb) { s_sleepy_cb = cb; }
bool zigate_radio_fail_quiet(void) { return s_fail_quiet; }

static void notify_radio(uint16_t msg, uint16_t short_addr, uint8_t status)
{
    if (s_radio_cb) s_radio_cb(msg, short_addr, status);
}

/**
 * Note qu'un appareil vient d'emettre : date du dernier contact, LQI et
 * etat radio remis a "joignable". En RAM uniquement (voir zigate_device_t).
 */
static void touch_device(uint16_t short_addr, uint8_t lqi)
{
    zigate_device_t *dev = zigate_devices_find_by_short(short_addr);
    if (!dev) return;
    dev->last_rx_s = zigate_uptime_s();
    dev->radio_status = 0;
    if (lqi) dev->lqi = lqi;
    time_t now = time(NULL);
    if (now > 1600000000) dev->last_seen = (uint32_t)now;
}

// --- Nettoyage des fantomes (reponse 0x8602) ---
// Table JN5189 : en-tete de 64 octets puis N entrees de 16 octets
// (IEEE little-endian sur 8 octets, adresse courte sur 2, bourrage). La table
// est renvoyee entiere (0x0603), entrees inconnues mises a zero : la tronquer
// faisait redemarrer la ZiGate (constat de la LiXee-Box).
static void handle_recovery_table(const zigate_packet_t *pkt)
{
    const int HEADER = 64, ENTRY = 16;
    if (!g_zigate_coord.ghost_pending) return;
    g_zigate_coord.ghost_pending = false;
    if (pkt->length < HEADER + 10 || pkt->length > sizeof(pkt->payload)) {
        ESP_LOGW(TAG, "Table de recuperation invalide (%u octets)", (unsigned)pkt->length);
        g_zigate_coord.ghost_result = 0;
        return;
    }

    static uint8_t table[sizeof(((zigate_packet_t *)0)->payload)];   // tache unique
    memcpy(table, pkt->payload, pkt->length);

    uint64_t coord = 0;
    memcpy(&coord, &table[16], 8);
    int ghosts = 0;
    int count = (pkt->length - HEADER) / ENTRY;
    for (int d = 0; d < count; d++) {
        uint8_t *e = &table[HEADER + d * ENTRY];
        uint64_t ieee = 0;
        uint16_t sa = 0;
        memcpy(&ieee, e, 8);
        memcpy(&sa, e + 8, 2);
        if (ieee == 0 || sa == 0 || ieee == coord) continue;

        // Texte de debug du JN5189 parfois present dans la table : une IEEE
        // valide a au moins un octet > 0x7E. Ces entrees ne sont pas touchees.
        bool valid = false;
        for (int b = 0; b < 8; b++) if (e[b] > 0x7E) { valid = true; break; }
        if (!valid) continue;

        // Doublon d'une entree precedente : efface
        bool dup = false;
        for (int k = 0; k < d; k++) {
            uint64_t other = 0;
            memcpy(&other, &table[HEADER + k * ENTRY], 8);
            if (other == ieee) { dup = true; break; }
        }
        if (dup || !zigate_devices_find_by_ieee(ieee)) {
            ESP_LOGW(TAG, "Fantome efface : SA=0x%04X IEEE=%016llX%s", sa,
                     (unsigned long long)ieee, dup ? " (doublon)" : "");
            memset(e, 0, ENTRY);
            ghosts++;
        }
    }
    g_zigate_coord.ghost_result = ghosts;
    zigate_console_printf("Nettoyage des fantomes : %d entree(s) effacee(s)", ghosts);
    if (ghosts > 0) {
        zigate_send_command(ZIGATE_CMD_NWK_RECOVERY_RESTORE, table, pkt->length);
    }
}

// --- Datapoints Tuya (cluster 0xEF00, recus en trame brute 0x8002) ---
// Chaque datapoint est remonte comme un attribut du cluster 0xEF00 dont
// l'identifiant est le numero de DP : il suit ensuite le chemin normal
// (device_data, MQTT...), comme sur la LiXee-Box ou les templates Tuya
// designent un DP par "cluster": "EF00", "attribut": <dp>.
static uint8_t tuya_type_to_zcl(uint8_t dp_type, uint16_t len)
{
    switch (dp_type) {
    case 0x01: return 0x10;                  // bool
    case 0x02: return 0x2B;                  // valeur : entier signe 32 bits
    case 0x03: return 0x42;                  // chaine
    case 0x04: return 0x30;                  // enum8
    case 0x05: return len >= 4 ? 0x1B : (len == 2 ? 0x19 : 0x18);   // bitmap
    default:   return 0x41;                  // brut : chaine d'octets
    }
}

/**
 * Reponse a la demande d'heure Tuya (commande 0x24, mcuSyncTime de
 * zigbee2mqtt) : taille 8, puis heures UTC et locale en secondes depuis le
 * 01/01/2000, big-endian. Thermostats et afficheurs Tuya la demandent au
 * demarrage (sendTuyaTimeSync de la LiXee-Box, qui ajoutait 1 h fixe).
 */
static void tuya_time_sync(uint16_t short_addr, uint8_t ep, uint8_t zcl_seq)
{
    time_t now = time(NULL);
    if (now < 1704067200) return;            // heure pas encore synchronisee (NTP)
    struct tm lt, gt;
    localtime_r(&now, &lt);
    gmtime_r(&now, &gt);
    gt.tm_isdst = lt.tm_isdst;
    long offset = (long)(now - mktime(&gt)); // decalage du fuseau, heure d'ete comprise
    uint32_t utc = (uint32_t)(now - 946684800);
    uint32_t loc = (uint32_t)((int64_t)utc + offset);
    uint8_t zcl[13] = {
        0x19, zcl_seq, 0x24,                 // propre au cluster, serveur -> client, sans reponse par defaut
        0x00, 0x08,
        (uint8_t)(utc >> 24), (uint8_t)(utc >> 16), (uint8_t)(utc >> 8), (uint8_t)utc,
        (uint8_t)(loc >> 24), (uint8_t)(loc >> 16), (uint8_t)(loc >> 8), (uint8_t)loc,
    };
    esp_err_t e = zigate_send_raw_aps(short_addr, 0x01, ep ? ep : 1, 0xEF00, 0x0104, 0x1E, zcl, sizeof(zcl));
    ESP_LOGI(TAG, "Tuya : heure envoyee a 0x%04X (%s)", short_addr, esp_err_to_name(e));
}

static void handle_tuya_frame(const zigate_packet_t *pkt, uint16_t short_addr, uint8_t ep, int idx)
{
    const uint8_t *p = pkt->payload;
    int len = pkt->length - 1;               // dernier octet : LQI
    if (idx + 3 > len) return;
    uint8_t fc = p[idx++];
    if (fc & 0x04) idx += 2;                 // code fabricant present
    if (idx + 2 > len) return;
    uint8_t seq = p[idx++];                  // numero de sequence ZCL
    uint8_t cmd = p[idx++];
    if (cmd == 0x24) {
        tuya_time_sync(short_addr, ep, seq);
        return;
    }
    // 0x01 reponse, 0x02 rapport, 0x06 rapport d'etat : meme format
    if (cmd != 0x01 && cmd != 0x02 && cmd != 0x06) return;
    idx += 2;                                // sequence Tuya
    while (idx + 4 <= len) {
        uint8_t dp = p[idx], type = p[idx + 1];
        uint16_t dlen = (uint16_t)((p[idx + 2] << 8) | p[idx + 3]);
        idx += 4;
        if (idx + dlen > len) break;
        if (s_attr_cb) {
            zigate_attribute_report_t r = {};
            r.short_addr = short_addr;
            r.endpoint   = ep;
            r.cluster_id = 0xEF00;
            r.attr_id    = dp;
            r.attr_type  = tuya_type_to_zcl(type, dlen);
            r.data_len   = dlen > sizeof(r.data) ? sizeof(r.data) : dlen;
            memcpy(r.data, &p[idx], r.data_len);
            s_attr_cb(&r);
        }
        idx += dlen;
    }
}
// --- Commandes ZCL globales en trame brute (0x8002) ---
// Read Attributes Response (0x01) et Report Attributes (0x0A) d'un cluster que
// la ZiGate n'interprete pas (ZLinky : FF66 = TIC, dont le mode 0x0300). Comme
// la LiXee-Box (case 0x8002 de protocol.cpp), mais chaque enregistrement de la
// trame est remonte, pas seulement le premier. Valeurs remises au format des
// rapports 0x8102 : nombres en big-endian (ZCL : little-endian), chaines sans
// octet de longueur.
static void handle_zcl_global_frame(const zigate_packet_t *pkt, uint16_t short_addr, uint8_t ep,
                                    uint16_t cluster, int idx)
{
    const uint8_t *p = pkt->payload;
    const int end = pkt->length - 1;         // dernier octet : LQI
    if (idx + 3 > end) return;
    uint8_t fc = p[idx++];
    if (fc & 0x04) idx += 2;                 // code fabricant
    if (idx + 2 > end) return;
    idx++;                                   // numero de sequence ZCL
    uint8_t cmd = p[idx++];
    if ((fc & 0x03) == 0x01) {
        // Commande propre au cluster (mode brut : la ZiGate ne la decode pas),
        // comme la LiXee-Box : bouton (On/Off) et changement de zone IAS
        if (cluster == 0x0006 && cmd <= 0x02 && s_onoff_cmd_cb) {
            s_onoff_cmd_cb(short_addr, ep, cmd);
        } else if (cluster == 0x0500 && cmd == 0x00 && idx + 2 <= end) {
            emit_zone_status(short_addr, ep, (uint16_t)(p[idx] | (p[idx + 1] << 8)));
        }
        return;
    }
    if ((fc & 0x03) != 0x00) return;
    if (cmd != 0x01 && cmd != 0x0A) return;

    while (idx + 3 <= end) {
        uint16_t attr = (uint16_t)(p[idx] | (p[idx + 1] << 8));
        idx += 2;
        if (cmd == 0x01) {                   // reponse de lecture : statut
            uint8_t status = p[idx++];
            if (status != 0) continue;       // attribut absent : pas de valeur
            if (idx >= end) break;
        }
        uint8_t type = p[idx++];

        int vlen, skip = 0;
        if (type == 0x41 || type == 0x42) {  // chaine courte : longueur sur 1 octet
            if (idx >= end) break;
            vlen = p[idx] == 0xFF ? 0 : p[idx];
            skip = 1;
        } else if (type == 0x43 || type == 0x44) {   // chaine longue : 2 octets
            if (idx + 1 >= end) break;
            vlen = p[idx] | (p[idx + 1] << 8);
            if (vlen == 0xFFFF) vlen = 0;
            skip = 2;
        } else {
            vlen = zigate_zcl_type_size(type);
            if (vlen <= 0) break;            // tableau, structure... : fin de l'analyse
        }
        idx += skip;
        if (idx + vlen > end) break;

        zigate_attribute_report_t r = {};
        r.short_addr = short_addr;
        r.endpoint   = ep;
        r.cluster_id = cluster;
        r.attr_id    = attr;
        r.attr_type  = type;
        r.data_len   = (uint16_t)(vlen > (int)sizeof(r.data) ? sizeof(r.data) : vlen);
        bool is_str = (type >= 0x41 && type <= 0x44);
        for (int i = 0; i < r.data_len; i++) {
            r.data[i] = is_str ? p[idx + i] : p[idx + vlen - 1 - i];
        }
        idx += vlen;

        ESP_LOGI(TAG, "ZCL brut 0x%04X ep=%u cl=%04X at=%04X type=%02X len=%u (%s)",
                 short_addr, ep, cluster, attr, type, r.data_len,
                 cmd == 0x01 ? "lecture" : "rapport");
        if (s_attr_cb) s_attr_cb(&r);
    }
}

/** Etat de zone IAS (0x8401 ou commande 0x00 du cluster 0x0500) : attribut 0500/0002. */
static void emit_zone_status(uint16_t sa, uint8_t ep, uint16_t zs)
{
    if (!s_attr_cb) return;
    zigate_attribute_report_t r = {};
    r.short_addr = sa;
    r.endpoint   = ep;
    r.cluster_id = 0x0500;
    r.attr_id    = 0x0002;
    r.attr_type  = 0x19;                     // bitmap16
    r.data[0]    = (uint8_t)(zs >> 8);
    r.data[1]    = (uint8_t)zs;
    r.data_len   = 2;
    ESP_LOGI(TAG, "Zone IAS 0x%04X ep=%u : 0x%04X", sa, ep, zs);
    s_attr_cb(&r);
}

static TickType_t s_discovery_start_tick = 0;

// ============================================================================
// Checksum
// ============================================================================

uint8_t zigate_compute_checksum(uint16_t type, uint16_t length, const uint8_t *payload)
{
    uint8_t chk = 0;
    chk ^= (type >> 8) & 0xFF;
    chk ^= type & 0xFF;
    chk ^= (length >> 8) & 0xFF;
    chk ^= length & 0xFF;
    for (uint16_t i = 0; i < length; i++) {
        chk ^= payload[i];
    }
    return chk;
}

// ============================================================================
// Encodage (byte-stuffing)
// Chaque octet >= 0x10 est envoye tel quel
// Les octets 0x00-0x0F sont encodes comme: 0x02, (octet ^ 0x10)
// ============================================================================

static void encode_byte(uint8_t b, uint8_t *out, size_t *pos)
{
    if (b < 0x10) {
        out[(*pos)++] = ZIGATE_ESCAPE;
        out[(*pos)++] = b ^ 0x10;
    } else {
        out[(*pos)++] = b;
    }
}

esp_err_t zigate_encode_packet(const zigate_packet_t *pkt, uint8_t *out_buf,
                               size_t out_cap, size_t *out_len)
{
    size_t pos = 0;

    if (!pkt || !out_buf || !out_len) return ESP_ERR_INVALID_ARG;

    // Un paquet ne peut pas annoncer plus de payload qu'il n'en contient :
    // sinon la boucle d'encodage et le checksum liraient hors de payload[].
    if (pkt->length > sizeof(pkt->payload)) {
        ESP_LOGW(TAG, "Payload too long: %u bytes (max %u), dropping",
                 (unsigned)pkt->length, (unsigned)sizeof(pkt->payload));
        return ESP_ERR_INVALID_SIZE;
    }

    // Pire cas d'encodage : tout octet < 0x10 est emis sur 2 octets.
    // 1 (start) + 2*2 (type) + 2*2 (length) + 2 (checksum) + 2*length + 1 (end)
    const size_t worst_case = 2u * (size_t)pkt->length + 12u;
    if (out_cap < worst_case) {
        ESP_LOGW(TAG, "Output buffer too small: %u bytes, need %u",
                 (unsigned)out_cap, (unsigned)worst_case);
        return ESP_ERR_INVALID_SIZE;
    }

    // Start delimiter
    out_buf[pos++] = ZIGATE_MSG_START;

    // Type (2 bytes)
    encode_byte((pkt->cmd >> 8) & 0xFF, out_buf, &pos);
    encode_byte(pkt->cmd & 0xFF, out_buf, &pos);

    // Length (2 bytes)
    encode_byte((pkt->length >> 8) & 0xFF, out_buf, &pos);
    encode_byte(pkt->length & 0xFF, out_buf, &pos);

    // Checksum
    uint8_t chk = zigate_compute_checksum(pkt->cmd, pkt->length, pkt->payload);
    encode_byte(chk, out_buf, &pos);

    // Payload
    for (uint16_t i = 0; i < pkt->length; i++) {
        encode_byte(pkt->payload[i], out_buf, &pos);
    }

    // End delimiter
    out_buf[pos++] = ZIGATE_MSG_END;

    *out_len = pos;
    return ESP_OK;
}

// ============================================================================
// Decodage (byte-unstuffing)
// L'input est le contenu ENTRE 0x01 et 0x03 (sans ces delimiteurs)
// ============================================================================

esp_err_t zigate_decode_packet(const uint8_t *raw, size_t raw_len, zigate_packet_t *pkt)
{
    if (!raw || !pkt) return ESP_ERR_INVALID_ARG;

    // D'abord, de-escape le buffer.
    // Le de-escaping ne produit jamais plus d'octets qu'il n'en consomme
    // (0x02 suivi d'un octet echappe -> 1 octet), donc dlen <= raw_len et un
    // buffer dimensionne comme le buffer de reception ne peut pas deborder.
    // La fonction etant publique, on refuse explicitement une trame plus longue
    // au lieu de se reposer sur le seul appelant actuel.
    uint8_t decoded[ZIGATE_BUF_SIZE];
    size_t  dlen = 0;

    if (raw_len > sizeof(decoded)) {
        ESP_LOGW(TAG, "Frame too long: %u bytes (max %u), dropping",
                 (unsigned)raw_len, (unsigned)sizeof(decoded));
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < raw_len; i++) {
        if (raw[i] == ZIGATE_ESCAPE && (i + 1) < raw_len) {
            decoded[dlen++] = raw[i + 1] ^ 0x10;
            i++;
        } else {
            decoded[dlen++] = raw[i];
        }
    }

    if (dlen < 5) return ESP_ERR_INVALID_SIZE;

    // Parser: Type(2) + Length(2) + Checksum(1) + Payload(Length) + LQI(1 optionnel)
    pkt->cmd    = (decoded[0] << 8) | decoded[1];
    pkt->length = (decoded[2] << 8) | decoded[3];
    pkt->checksum = decoded[4];

    if (pkt->length > sizeof(pkt->payload)) return ESP_ERR_INVALID_SIZE;

    size_t payload_start = 5;
    if (payload_start + pkt->length > dlen) {
        // Le LQI peut ne pas etre present
        pkt->length = dlen - payload_start;
    }

    memcpy(pkt->payload, &decoded[payload_start], pkt->length);

    // LQI : DERNIER octet du payload, compte dans la longueur annoncee par la
    // ZiGate (0x8102 : 12 octets d'en-tete + donnees + LQI, lu par la
    // LiXee-Box en payload[12 + longueur]). Il etait cherche apres le payload,
    // ou il n'y a rien : le LQI valait toujours 0. Significatif seulement pour
    // les trames venant de la radio (rapports, annonces, trames brutes).
    pkt->lqi = pkt->length ? pkt->payload[pkt->length - 1] : 0;

    // Verifier le checksum
    uint8_t expected_chk = zigate_compute_checksum(pkt->cmd, pkt->length, pkt->payload);
    if (pkt->checksum != expected_chk) {
        ESP_LOGW(TAG, "CRC mismatch: got=0x%02X expected=0x%02X cmd=0x%04X",
                 pkt->checksum, expected_chk, pkt->cmd);
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

// ============================================================================
// Dispatch - Route les paquets vers les handlers
// ============================================================================

void zigate_dispatch_packet(const zigate_packet_t *pkt)
{
    // Log toutes les reponses en INFO pour le debug initial
    ESP_LOGI(TAG, "RX cmd=0x%04X len=%d lqi=%d", pkt->cmd, pkt->length, pkt->lqi);
    g_zigate_coord.frames_rx++;
    zigate_console_frame('<', pkt);

    switch (pkt->cmd) {

    case ZIGATE_RESP_STATUS: {
        // Status de la derniere commande: payload[0]=status, payload[1..2]=seq, payload[3..4]=cmd
        uint8_t status = pkt->payload[0];
        uint16_t seq_num = (pkt->length >= 3) ? ((pkt->payload[1] << 8) | pkt->payload[2]) : 0;
        uint16_t for_cmd = (pkt->length >= 5) ? ((pkt->payload[3] << 8) | pkt->payload[4]) : 0;
        if (status == 0) {
            ESP_LOGI(TAG, "Status OK (seq=%d, cmd=0x%04X)", seq_num, for_cmd);
        } else {
            ESP_LOGW(TAG, "Status ERROR=%d (seq=%d, cmd=0x%04X)", status, seq_num, for_cmd);
        }
        // Format documente par la ZiGate : status(1) seq(1) type du paquet(2).
        // Le journal ci-dessus lit le type un octet plus loin ; il est garde
        // tel quel, l'observateur recoit la valeur documentee.
        if (s_status_cb && pkt->length >= 4) {
            s_status_cb(status, (uint16_t)((pkt->payload[2] << 8) | pkt->payload[3]));
        }
        break;
    }

    case ZIGATE_RESP_VERSION: {
        if (pkt->length >= 4) {
            memcpy(g_zigate_coord.version, pkt->payload, 4);
            g_zigate_coord.version_known = true;
        }
        if (pkt->length >= 3) {
            ESP_LOGI(TAG, "ZiGate FW version: %02X.%02X (SDK %02X)",
                     pkt->payload[1], pkt->payload[2], pkt->payload[0]);
        } else {
            ESP_LOGI(TAG, "ZiGate version response (len=%d)", pkt->length);
        }
        break;
    }

    case ZIGATE_RESP_LOG: {
        // Log message de la ZiGate
        ESP_LOGI(TAG, "ZiGate LOG: level=%d (len=%d)",
                 (pkt->length > 0) ? pkt->payload[0] : -1, pkt->length);
        break;
    }

    case ZIGATE_RESP_DEVICE_ANNOUNCE: {
        // payload: shortAddr(2) + ieeeAddr(8) + macCapability(1)
        if (pkt->length >= 11) {
            uint16_t short_addr = (pkt->payload[0] << 8) | pkt->payload[1];
            uint64_t ieee = 0;
            for (int i = 0; i < 8; i++) {
                ieee = (ieee << 8) | pkt->payload[2 + i];
            }
            uint8_t mac_cap = pkt->payload[10];
            ESP_LOGI(TAG, "Device Announce: short=0x%04X ieee=0x%016llX macCap=0x%02X",
                     short_addr, ieee, mac_cap);

            // Pour l'assistant d'appairage : appareil arrive ou revenu
            g_zigate_coord.last_join_ieee  = ieee;
            g_zigate_coord.last_join_short = short_addr;
            g_zigate_coord.last_join_s     = zigate_uptime_s();
            g_zigate_coord.join_seq++;

            // Enregistrer dans la base des devices
            zigate_device_t dev = {};
            dev.ieee_addr = ieee;
            dev.short_addr = short_addr;
            dev.active = true;
            dev.last_seen = 0;
            zigate_devices_add_or_update(&dev);
            touch_device(short_addr, pkt->lqi);

            // Callback
            if (s_announce_cb) {
                s_announce_cb(short_addr, ieee);
            }

            // Protection contre la double decouverte:
            // Si on est deja en train de decouvrir ce device (dans les 10 dernières secondes), ignorer
            TickType_t now = xTaskGetTickCount();
            if (s_discovery_in_progress_addr == short_addr &&
                (now - s_discovery_start_tick) < pdMS_TO_TICKS(10000)) {
                ESP_LOGW(TAG, "Discovery already in progress for 0x%04X, skipping duplicate", short_addr);
                break;
            }
            s_discovery_in_progress_addr = short_addr;
            s_discovery_start_tick = now;

            // Lancer la decouverte automatique apres un delai de securite
            // (laisser le temps au Transport Key d'etre envoye)
            ESP_LOGI(TAG, "Starting discovery for 0x%04X in 1500ms...", short_addr);
            vTaskDelay(pdMS_TO_TICKS(1500));
            zigate_request_active_endpoints(short_addr);
        }
        break;
    }

    case ZIGATE_RESP_ACTIVE_ENDPOINT: {
        // payload: seq(1) + status(1) + shortAddr(2) + epCount(1) + epList(N)
        // OU format alternatif: status(1) + shortAddr(2) + epCount(1) + epList(N)
        if (pkt->length >= 4) {
            int idx = 0;
            // Le premier octet peut etre le seq number ou le status
            // Le status est dans payload[0] ou payload[1] selon la version FW
            uint8_t status;
            uint16_t short_addr;
            uint8_t ep_count;

            // Format ZiGate v3/v5: seq(1) + status(1) + shortAddr(2) + epCount(1) + eps
            if (pkt->length >= 5) {
                idx = 0;
                idx++; // skip seq
                status = pkt->payload[idx++];
                short_addr = (pkt->payload[idx] << 8) | pkt->payload[idx + 1]; idx += 2;
                ep_count = pkt->payload[idx++];
            } else {
                status = pkt->payload[0];
                short_addr = (pkt->payload[1] << 8) | pkt->payload[2];
                ep_count = pkt->payload[3];
                idx = 4;
            }

            ESP_LOGI(TAG, "Active Endpoints: status=%d addr=0x%04X count=%d",
                     status, short_addr, ep_count);

            if (status == 0 && ep_count > 0) {
                // Pour chaque endpoint, demander le Simple Descriptor
                for (int i = 0; i < ep_count && (idx + i) < pkt->length; i++) {
                    uint8_t ep = pkt->payload[idx + i];
                    ESP_LOGI(TAG, "  Endpoint %d: %d", i, ep);
                    // Petit delai entre chaque requete
                    vTaskDelay(pdMS_TO_TICKS(300));
                    zigate_request_simple_descriptor(short_addr, ep);
                }
            }
        }
        break;
    }

    case ZIGATE_RESP_SIMPLE_DESC: {
        // payload: seq(1) + status(1) + shortAddr(2) + length(1) + endpoint(1)
        //          + profileId(2) + deviceId(2) + bitfield(1)
        //          + inClusterCount(1) + inClusters(N*2)
        //          + outClusterCount(1) + outClusters(N*2)
        // L'en-tete consomme 11 octets (indices 0 a 10, dev_version inclus).
        // Avec length == 10, dev_version lisait un octet au-dela de la trame.
        if (pkt->length >= 11) {
            int idx = 0;
            idx++; // skip seq
            uint8_t status = pkt->payload[idx++];
            uint16_t short_addr = (pkt->payload[idx] << 8) | pkt->payload[idx + 1]; idx += 2;
            idx++;                                   // longueur du descripteur (inutilisee)
            uint8_t endpoint = pkt->payload[idx++];
            uint16_t profile_id = (pkt->payload[idx] << 8) | pkt->payload[idx + 1]; idx += 2;
            uint16_t device_id = (pkt->payload[idx] << 8) | pkt->payload[idx + 1]; idx += 2;
            uint8_t dev_version = pkt->payload[idx++];

            ESP_LOGI(TAG, "Simple Desc: status=%d addr=0x%04X ep=%d profile=0x%04X devId=0x%04X ver=%d",
                     status, short_addr, endpoint, profile_id, device_id, dev_version);

            if (status == 0) {
                // Mettre a jour le device dans la base
                zigate_device_t *dev = zigate_devices_find_by_short(short_addr);
                if (dev) {
                    // Ignorer les endpoints non-Home Automation (ex: Green Power ep=242, profile=0xA1E0)
                    // pour ne pas ecraser les clusters de l'endpoint applicatif principal
                    bool is_ha_endpoint = (profile_id == 0x0104 || profile_id == 0xC05E) && endpoint != 242;

                    if (is_ha_endpoint) {
                        dev->device_id = device_id;
                        dev->profile_id = profile_id;
                        dev->endpoint = endpoint;
                    } else {
                        ESP_LOGI(TAG, "Non-HA endpoint %d (profile=0x%04X), skipping cluster update",
                                 endpoint, profile_id);
                    }

                    const int max_in  = (int)(sizeof(dev->in_clusters)  / sizeof(dev->in_clusters[0]));
                    const int max_out = (int)(sizeof(dev->out_clusters) / sizeof(dev->out_clusters[0]));

                    // Aucun compteur ne doit survivre a une trame qui ne le reecrit
                    // pas (trame s'arretant apres l'en-tete, ou bloc OUT absent) :
                    // on repart de zero et on ne declare que ce qui est lu.
                    if (is_ha_endpoint) {
                        dev->in_cluster_count  = 0;
                        dev->out_cluster_count = 0;
                    }

                    // Parser les clusters IN (toujours les loguer, mais ne les stocker que pour HA)
                    // Le compteur n'est publie qu'APRES la boucle, avec le nombre
                    // reellement lu : sur trame tronquee, l'ancien code annoncait
                    // le compte de la trame et laissait les entrees non reecrites
                    // a leur valeur precedente.
                    bool in_complete = true;
                    if (idx < pkt->length) {
                        uint8_t nb_in = pkt->payload[idx++];
                        ESP_LOGI(TAG, "  In clusters (%d):", nb_in);
                        int read_in = 0;
                        for (int i = 0; i < nb_in && idx + 1 < pkt->length; i++) {
                            uint16_t cl = (pkt->payload[idx] << 8) | pkt->payload[idx + 1]; idx += 2;
                            if (is_ha_endpoint && i < max_in) dev->in_clusters[i] = cl;
                            read_in++;
                            ESP_LOGI(TAG, "    0x%04X", cl);
                        }
                        if (read_in < nb_in) {
                            ESP_LOGW(TAG, "Truncated Simple Desc: %d/%d in clusters read",
                                     read_in, nb_in);
                            in_complete = false;
                        }
                        if (is_ha_endpoint) {
                            dev->in_cluster_count = (read_in > max_in) ? max_in : read_in;
                        }
                    }

                    // Parser les clusters OUT, uniquement si le bloc IN est complet :
                    // sinon idx est desynchronise et l'octet lu serait la moitie
                    // d'un identifiant de cluster, pas un compteur.
                    if (in_complete && idx < pkt->length) {
                        uint8_t nb_out = pkt->payload[idx++];
                        ESP_LOGI(TAG, "  Out clusters (%d):", nb_out);
                        int read_out = 0;
                        for (int i = 0; i < nb_out && idx + 1 < pkt->length; i++) {
                            uint16_t cl = (pkt->payload[idx] << 8) | pkt->payload[idx + 1]; idx += 2;
                            if (is_ha_endpoint && i < max_out) dev->out_clusters[i] = cl;
                            read_out++;
                            ESP_LOGI(TAG, "    0x%04X", cl);
                        }
                        if (read_out < nb_out) {
                            ESP_LOGW(TAG, "Truncated Simple Desc: %d/%d out clusters read",
                                     read_out, nb_out);
                        }
                        if (is_ha_endpoint) {
                            dev->out_cluster_count = (read_out > max_out) ? max_out : read_out;
                        }
                    }

                    // Sauvegarder les modifications
                    zigate_devices_save();

                    ESP_LOGI(TAG, "Device 0x%04X discovery complete: devId=0x%04X ep=%d",
                             short_addr, device_id, endpoint);

                    // Correction : l'IEEE est copiee AVANT les attentes ci-dessous.
                    // 'dev' pointe dans la table de zigate_devices, qu'une
                    // suppression decale : apres les vTaskDelay, dev->ieee_addr
                    // pouvait designer un autre appareil (ou une case videe).
                    const uint64_t ieee = dev->ieee_addr;
                    dev = NULL;                     // ne plus l'utiliser au-dela

                    // Pour les endpoints HA: lire les attributs Basic et signaler la decouverte
                    if (is_ha_endpoint) {
                        // Lire les attributs Basic (manufacturer, model)
                        vTaskDelay(pdMS_TO_TICKS(300));
                        zigate_read_basic_attributes(short_addr, endpoint);

                        // Signaler que la decouverte est terminee
                        if (s_discovery_done_cb) {
                            s_discovery_done_cb(short_addr, ieee);
                        }
                    }
                } else {
                    ESP_LOGW(TAG, "Device 0x%04X not found in database for Simple Desc update", short_addr);
                }
            }
        }
        break;
    }

    case ZIGATE_RESP_ATTRIBUTE_REPORT:
    case ZIGATE_RESP_READ_ATTRIBUTE: {
        // Rapport d'attribut ou reponse de lecture
        // Format: seq(1) + srcAddr(2) + ep(1) + clusterId(2) + attrId(2) + status(1)
        //         + attrType(1) + attrLen(2) + attrData(N)
        if (pkt->length >= 12) {
            zigate_attribute_report_t report = {};
            int idx = 0;
            idx++;  // seq
            report.short_addr = (pkt->payload[idx] << 8) | pkt->payload[idx + 1]; idx += 2;
            report.endpoint   = pkt->payload[idx++];
            report.cluster_id = (pkt->payload[idx] << 8) | pkt->payload[idx + 1]; idx += 2;
            report.attr_id    = (pkt->payload[idx] << 8) | pkt->payload[idx + 1]; idx += 2;
            uint8_t status    = pkt->payload[idx++];

            // Toute reponse prouve que l'appareil est joignable, meme en erreur
            touch_device(report.short_addr, pkt->lqi);

            if (status == 0) {  // Success
                report.attr_type = pkt->payload[idx++];
                report.data_len  = (pkt->payload[idx] << 8) | pkt->payload[idx + 1]; idx += 2;
                if (report.data_len > sizeof(report.data)) {
                    report.data_len = sizeof(report.data);
                }
                // Correction : sur une trame tronquee, la copie etait sautee mais
                // data_len gardait la longueur annoncee : le rapport partait avec
                // des octets a zero jamais recus (valeur 0 enregistree, publiee
                // en MQTT, regles declenchees). Le rapport est desormais ignore.
                if (idx + report.data_len > pkt->length) {
                    ESP_LOGW(TAG, "Attr frame truncated: addr=%04X cl=%04X at=%04X len=%u, only %d bytes, dropped",
                             report.short_addr, report.cluster_id, report.attr_id,
                             (unsigned)report.data_len, pkt->length - idx);
                    break;
                }
                memcpy(report.data, &pkt->payload[idx], report.data_len);

                ESP_LOGI(TAG, "Attr: addr=%04X ep=%d cl=%04X at=%04X type=%02X len=%d",
                         report.short_addr, report.endpoint, report.cluster_id,
                         report.attr_id, report.attr_type, report.data_len);

                // Traitement special pour le Basic Cluster (0x0000)
                // Stocker manufacturer/model dans la base des devices
                if (report.cluster_id == 0x0000) {
                    zigate_device_t *dev = zigate_devices_find_by_short(report.short_addr);
                    if (dev) {
                        if (report.attr_id == 0x0004 && report.data_len > 0) {
                            // Manufacturer Name (string)
                            size_t copy_len = (report.data_len > 32) ? 32 : report.data_len;
                            memcpy(dev->manufacturer, report.data, copy_len);
                            dev->manufacturer[copy_len] = '\0';
                            ESP_LOGI(TAG, "Device 0x%04X manufacturer: %s",
                                     report.short_addr, dev->manufacturer);
                            zigate_devices_save();
                        } else if (report.attr_id == 0x0005 && report.data_len > 0) {
                            // Model Identifier (string)
                            size_t copy_len = (report.data_len > 32) ? 32 : report.data_len;
                            memcpy(dev->model, report.data, copy_len);
                            dev->model[copy_len] = '\0';
                            ESP_LOGI(TAG, "Device 0x%04X model: %s",
                                     report.short_addr, dev->model);
                            zigate_devices_save();
                        }
                    }
                }

                if (s_attr_cb) {
                    s_attr_cb(&report);
                }
            }
        }
        break;
    }

    case ZIGATE_RESP_LEAVE_IND: {
        // Device leave: ieeeAddr(8)
        if (pkt->length >= 8) {
            uint64_t ieee = 0;
            for (int i = 0; i < 8; i++) {
                ieee = (ieee << 8) | pkt->payload[i];
            }
            ESP_LOGW(TAG, "Device Leave: ieee=0x%016llX", ieee);
            if (s_leave_cb) {
                s_leave_cb(ieee);
            }
        }
        break;
    }

    case ZIGATE_RESP_NETWORK_STATE: {
        // Payload: shortAddr(2) + ieeeAddr(8) + panId(2) + extPanId(8) + channel(1)
        if (pkt->length >= 13) {
            uint16_t short_addr = (pkt->payload[0] << 8) | pkt->payload[1];
            uint64_t ieee = 0;
            for (int i = 0; i < 8; i++) {
                ieee = (ieee << 8) | pkt->payload[2 + i];
            }
            uint16_t pan_id = (pkt->payload[10] << 8) | pkt->payload[11];
            uint8_t channel = (pkt->length >= 22) ? pkt->payload[20] : 0;
            ESP_LOGI(TAG, "Network State: coord=0x%04X ieee=0x%016llX panId=0x%04X ch=%d",
                     short_addr, ieee, pan_id, channel);
            if (ieee != 0) s_coordinator_ieee = ieee;
            uint64_t ext = 0;
            if (pkt->length >= 20) {
                for (int i = 0; i < 8; i++) ext = (ext << 8) | pkt->payload[12 + i];
            }
            g_zigate_coord.short_addr = short_addr;
            g_zigate_coord.ieee       = ieee;
            g_zigate_coord.pan_id     = pan_id;
            g_zigate_coord.ext_pan_id = ext;
            if (channel) g_zigate_coord.channel = channel;
            g_zigate_coord.state_known = true;
        } else {
            ESP_LOGI(TAG, "Network state received (len=%d)", pkt->length);
        }
        break;
    }

    case ZIGATE_RESP_PERMIT_JOIN_STATUS: {
        uint8_t status = (pkt->length > 0) ? pkt->payload[0] : 0xFF;
        ESP_LOGI(TAG, "Permit Join Status: %s", status ? "OPEN" : "CLOSED");
        if (status == 0) g_zigate_coord.permit_join_until = 0;
        break;
    }

    case 0x8024: {
        // Reseau demarre : status(1) short(2) ieee(8) canal(1)
        // 0 = rejoint, 1 = forme, >= 0x80 = echec
        if (pkt->length >= 1) g_zigate_coord.start_status = pkt->payload[0];
        if (pkt->length >= 12 && pkt->payload[11]) g_zigate_coord.channel = pkt->payload[11];
        ESP_LOGI(TAG, "Network started: status=%d ch=%d", g_zigate_coord.start_status,
                 g_zigate_coord.channel);
        break;
    }

    case 0x8501:            // OTA : demande de bloc d'image
    case 0x8503:            // OTA : fin de mise a jour
        if (pkt->length >= 7) touch_device((uint16_t)((pkt->payload[5] << 8) | pkt->payload[6]), 0);
        zigate_ota_handle_packet(pkt);
        break;

    case 0x8401: {
        // Changement d'etat de zone IAS (fuite d'eau, ouverture, mouvement) :
        // sqn(1) ep(1) cluster(2) mode(1) adresse(2) etat(2) [etendu zone delai]
        if (pkt->length >= 9) {
            uint16_t sa = (uint16_t)((pkt->payload[5] << 8) | pkt->payload[6]);
            touch_device(sa, pkt->length >= 10 ? pkt->payload[pkt->length - 1] : 0);
            emit_zone_status(sa, pkt->payload[1], (uint16_t)((pkt->payload[7] << 8) | pkt->payload[8]));
        }
        break;
    }

    case 0x8095: {
        // Commande On/Off emise par un bouton / une telecommande (SNZB-01P...),
        // decodee par la ZiGate : sqn(1) endpoint(1) cluster(2) mode(1)
        // adresse(2) commande(1) puis LQI
        if (pkt->length >= 8) {
            uint16_t sa = (uint16_t)((pkt->payload[5] << 8) | pkt->payload[6]);
            uint8_t lqi = pkt->length >= 9 ? pkt->payload[pkt->length - 1] : 0;
            touch_device(sa, lqi);
            if (s_onoff_cmd_cb) s_onoff_cmd_cb(sa, pkt->payload[1], pkt->payload[7]);
        }
        break;
    }

    case 0x8011: {
        // Accuse de reception applicatif : status(1) adresse(2) endpoint(1) cluster(2)
        if (pkt->length >= 3) {
            uint16_t sa = (uint16_t)((pkt->payload[1] << 8) | pkt->payload[2]);
            if (pkt->payload[0] == 0) touch_device(sa, 0);
            notify_radio(0x8011, sa, pkt->payload[0]);
        }
        break;
    }

    case 0x8012: {
        // Confirmation APS : status(1) srcEp(1) dstEp(1) mode(1) adresse(2 en mode court)
        if (pkt->length >= 6 && (pkt->payload[3] == 0x02 || pkt->payload[3] == 0x07)) {
            notify_radio(0x8012, (uint16_t)((pkt->payload[4] << 8) | pkt->payload[5]), pkt->payload[0]);
        }
        break;
    }

    case 0x8701: {
        // Echec de recherche de route : status(1) statut reseau(1) adresse(2)
        if (pkt->length >= 4) {
            notify_radio(0x8701, (uint16_t)((pkt->payload[2] << 8) | pkt->payload[3]), pkt->payload[0]);
        }
        break;
    }

    case 0x8702: {
        // Echec APS : status(1) srcEp(1) dstEp(1) mode(1) adresse(2 en mode court) seq(1)
        if (pkt->length >= 6 && (pkt->payload[3] == 0x02 || pkt->payload[3] == 0x07)) {
            uint16_t sa = (uint16_t)((pkt->payload[4] << 8) | pkt->payload[5]);
            uint8_t st = pkt->payload[0], dst_ep = pkt->payload[2];
            zigate_device_t *dev = zigate_devices_find_by_short(sa);
            // Requete reseau (ZDO, endpoint 0 : tables de voisinage que la
            // ZiGate interroge d'elle-meme chaque minute) vers un appareil sur
            // pile : il dort, l'echec est normal (F0 : expiree dans la file
            // d'attente du parent, propre aux appareils endormis). Ni etat
            // radio ni alerte ; les regles en sont informees (place liberee
            // dans la ZiGate). Sur secteur : signale, la route manque vraiment.
            bool quiet = dst_ep == 0 &&
                         (st == 0xF0 || (dev && s_sleepy_cb && s_sleepy_cb(dev->ieee_addr)));
            if (dev && !quiet) dev->radio_status = st;
            if (quiet) {
                ESP_LOGI(TAG, "APS fail 0x%02X vers 0x%04X ignore (requete reseau, appareil sur pile)", st, sa);
            } else {
                ESP_LOGW(TAG, "APS fail 0x%02X vers 0x%04X (ep %u -> %u)", st, sa, pkt->payload[1], dst_ep);
            }
            s_fail_quiet = quiet;
            notify_radio(0x8702, sa, st);
            s_fail_quiet = false;
        }
        break;
    }

    case 0x8602: {
        handle_recovery_table(pkt);
        break;
    }

    case ZIGATE_RESP_NODE_DESC: {
        ESP_LOGI(TAG, "Node descriptor received (len=%d)", pkt->length);
        break;
    }

    case 0x8002: {
        // Trame APS brute (cluster non interprete par la ZiGate) : contenu
        // journalise pour le diagnostic, comme le firmware Arduino.
        // status(1) profile(2) cluster(2) srcEp(1) dstEp(1) srcMode(1) src(2)...
        char hex[3 * 32 + 1];
        int n = pkt->length < 32 ? pkt->length : 32;
        for (int i = 0; i < n; i++) snprintf(&hex[3 * i], 4, " %02X", pkt->payload[i]);
        hex[3 * n] = '\0';
        uint16_t cluster = (pkt->length >= 5) ? ((pkt->payload[3] << 8) | pkt->payload[4]) : 0;
        ESP_LOGI(TAG, "Raw APS cl=0x%04X :%s", cluster, hex);
        // Source en adressage court : payload[7] = 0x02, adresse en [8..9],
        // puis mode et adresse de destination (2 ou 8 octets), puis trame ZCL
        if (pkt->length >= 13 && pkt->payload[7] == 0x02) {
            uint16_t sa = (uint16_t)((pkt->payload[8] << 8) | pkt->payload[9]);
            touch_device(sa, pkt->lqi);
            int zcl = 11 + (pkt->payload[10] == 0x03 ? 8 : 2);
            if (cluster == 0xEF00) {
                handle_tuya_frame(pkt, sa, pkt->payload[5], zcl);
            } else if (cluster == 0xED00 || cluster == 0xE004) {
                // Telecommande infrarouge Zosung : dernier octet = LQI
                if (pkt->length - 1 > zcl)
                    zigate_ir_handle_frame(sa, pkt->payload[5], cluster, &pkt->payload[zcl], pkt->length - 1 - zcl);
            } else {
                // Cluster fabricant (ZLinky FF66...) : la ZiGate ne decode pas
                // les reponses de lecture ni les rapports, ils arrivent ici
                handle_zcl_global_frame(pkt, sa, pkt->payload[5], cluster, zcl);
            }
        }
        break;
    }

    case 0x8030: {
        // Reponse au bind : seq(1) status(1) [mode(1) addr(2)] LQI
        uint8_t status = (pkt->length >= 2) ? pkt->payload[1] : 0xFF;
        if (status == 0) {
            ESP_LOGI(TAG, "Bind OK (seq=%u)", pkt->payload[0]);
        } else {
            ESP_LOGW(TAG, "Bind REFUSE status=0x%02X (seq=%u)", status, pkt->payload[0]);
        }
        break;
    }

    case 0x8120: {
        // Reponse a la configuration du reporting :
        // seq(1) addr(2) ep(1) cluster(2) status(1) LQI
        if (pkt->length >= 7) {
            uint16_t addr    = (pkt->payload[1] << 8) | pkt->payload[2];
            uint16_t cluster = (pkt->payload[4] << 8) | pkt->payload[5];
            uint8_t  status  = pkt->payload[6];
            // 0x86 = attribut non supporte, 0x8C = attribut non reportable
            if (status == 0) {
                ESP_LOGI(TAG, "Config report OK: addr=0x%04X cl=0x%04X (seq=%u)",
                         addr, cluster, pkt->payload[0]);
            } else {
                ESP_LOGW(TAG, "Config report REFUSE: addr=0x%04X cl=0x%04X status=0x%02X (seq=%u)",
                         addr, cluster, status, pkt->payload[0]);
            }
        } else {
            ESP_LOGI(TAG, "Config report response (len=%d)", pkt->length);
        }
        break;
    }

    default:
        ESP_LOGI(TAG, "Unhandled ZiGate response: 0x%04X (len=%d)", pkt->cmd, pkt->length);
        break;
    }
}

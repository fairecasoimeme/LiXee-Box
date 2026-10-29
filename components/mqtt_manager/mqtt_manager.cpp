// ============================================================================
// MQTT Manager - Publication des donnees Zigbee vers un broker MQTT
//
// Port du mqtt.cpp Arduino. Utilise esp_mqtt_client natif ESP-IDF.
// Supporte les 3 modes: HA (Home Assistant), TB (ThingsBoard), Custom.
// ============================================================================

#include "mqtt_manager.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "cJSON.h"

#include "config_manager.h"
#include "device_data.h"
#include "ntp_sync.h"
#include "zigate_devices.h"
#include "zigate_actions.h"

static const char *TAG = "MQTT_MGR";

// ============================================================================
// Etat interne
// ============================================================================

static esp_mqtt_client_handle_t s_client = NULL;
// Publications (taches ZiGate, LoRa, HTTP, principale) contre la destruction du
// client par reload_client() : sans verrou, un enqueue pouvait viser un client
// en cours de destruction
static SemaphoreHandle_t s_client_mtx = NULL;
static bool s_connected = false;
static bool s_initialized = false;
static bool s_started = false;

// Reconnexion
static uint32_t s_last_reconnect_ms = 0;
static int s_reconnect_attempts = 0;

// Health check
static uint32_t s_last_health_check_s = 0;
static int s_publish_errors = 0;

// Commandes
static mqtt_cmd_handler_t s_cmd_handler = NULL;

// URI construite
static char s_uri[128] = {};

// ============================================================================
// Helpers
// ============================================================================

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/**
 * Convertit une valeur hex en entier 64 bits, exactement.
 *
 * Remplace l'ancien hex_to_long(), qui passait par strtol sur 32 bits : il
 * saturait a LONG_MAX des qu'un index d'energie depassait 2^31, et ne pouvait
 * pas rendre de valeur negative faute de connaitre le type ZCL.
 *
 * N'est plus utilise que la ou le type ZCL n'est pas disponible. Sur le chemin
 * normal, la conversion typee est faite une seule fois dans main.cpp via
 * zcl_to_int64(), et la valeur arrive par num_value.
 */
static int64_t hex_to_int64(const char *hex)
{
    if (!hex || *hex == '\0') return 0;
    return (int64_t)strtoull(hex, NULL, 16);
}

/**
 * Ajoute du texte formate dans un buffer borne.
 *
 * snprintf rend la longueur VOULUE, pas la longueur ecrite : avancer de cette
 * valeur sans la verifier faisait passer dst au-dela de dst_end, et l'ecriture
 * du terminateur tombait alors hors du tableau. Rend false quand le buffer est
 * plein, pour que l'appelant arrete la substitution proprement.
 */
static bool append_fmt(char **dst, char *dst_end, const char *fmt, ...)
{
    size_t avail = (size_t)(dst_end - *dst);
    if (avail == 0) return false;

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(*dst, avail, fmt, ap);
    va_end(ap);

    if (n < 0) return false;
    if ((size_t)n >= avail) {
        *dst = dst_end;   // tronque : buffer plein
        return false;
    }
    *dst += n;
    return true;
}

/**
 * IEEE des topics, de la decouverte et des unique_id, comme la LiXee-Box :
 * minuscules pour un appareil Zigbee (getMacAddress, "%02x"), majuscules pour
 * un emetteur LoRa. Les commandes recues sont lues sans tenir compte de la casse.
 */
static void mqtt_ieee_str(uint64_t ieee_addr, char *buf, size_t len)
{
    snprintf(buf, len, zigate_devices_find_by_ieee(ieee_addr) ? "%016llx" : "%016llX",
             (unsigned long long)ieee_addr);
}

/**
 * Publie un message MQTT (wrapper avec gestion d'erreurs).
 * @return ESP_OK si publie avec succes
 */
static esp_err_t mqtt_publish(const char *topic, const char *payload, int qos, bool retain)
{
    if (!s_connected || !s_client) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_client_mtx && xSemaphoreTake(s_client_mtx, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    int msg_id = (s_connected && s_client)
        ? esp_mqtt_client_enqueue(s_client, topic, payload, 0, qos, retain ? 1 : 0, true)
        : -2;
    if (s_client_mtx) xSemaphoreGive(s_client_mtx);
    if (msg_id == -2) return ESP_ERR_INVALID_STATE;
    if (msg_id < 0) {
        s_publish_errors++;
        ESP_LOGW(TAG, "Publish failed (errors=%d): %s", s_publish_errors, topic);
        return ESP_FAIL;
    }

    // Reset erreurs sur succes
    s_publish_errors = 0;
    ESP_LOGD(TAG, "Published [%d]: %s -> %s", msg_id, topic, payload);
    return ESP_OK;
}

// ============================================================================
// Publication - Mode Home Assistant (HA)
// ============================================================================

/**
 * Topic: {header}{IEEE}_{cluster}_{attribute}/state
 * Payload: {"value_{cluster}_{attribute}": <value>}
 */
static esp_err_t publish_ha(const char *ieee, const char *cluster,
                             const char *attr, const char *value, bool is_string,
                             int64_t num_value)
{
    const cfg_general_t *gen = cfg_get_general();
    const char *header = gen->header_mqtt;

    // Construire le topic
    char topic[256];
    snprintf(topic, sizeof(topic), "%s%s_%s_%s/state", header, ieee, cluster, attr);

    // Construire le payload JSON
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    char key[64];
    snprintf(key, sizeof(key), "value_%s_%s", cluster, attr);

    if (is_string) {
        cJSON_AddStringToObject(root, key, value);
    } else {
        // double a 53 bits de mantisse : exact sur toute la plage utile ici
        cJSON_AddNumberToObject(root, key, (double)num_value);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;

    // QoS 1 pour les valeurs, comme la LiXee-Box (outbox bornee a 16 Ko)
    esp_err_t ret = mqtt_publish(topic, json, 1, false);
    free(json);
    return ret;
}

// ============================================================================
// Publication - Mode ThingsBoard (TB)
// ============================================================================

/**
 * Topic: {header} (ex: "v1/gateway/telemetry")
 * Payload: {"{IEEE}": [{"cluster_attribute": value}]}
 */
static esp_err_t publish_tb(const char *ieee, const char *cluster,
                             const char *attr, const char *value, bool is_string,
                             int64_t num_value)
{
    const cfg_general_t *gen = cfg_get_general();
    const char *header = gen->header_mqtt;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON *arr = cJSON_CreateArray();
    cJSON *item = cJSON_CreateObject();

    char key[64];
    snprintf(key, sizeof(key), "%s_%s", cluster, attr);

    if (is_string) {
        cJSON_AddStringToObject(item, key, value);
    } else {
        cJSON_AddNumberToObject(item, key, (double)num_value);
    }

    cJSON_AddItemToArray(arr, item);
    cJSON_AddItemToObject(root, ieee, arr);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;

    esp_err_t ret = mqtt_publish(header, json, 1, false);   // QoS 1 (LiXee-Box)
    free(json);
    return ret;
}

// ============================================================================
// Publication - Mode Custom
// ============================================================================

/**
 * Template avec substitutions: <IEEE>, <cluster>, <attribute>, <value>
 */
static esp_err_t publish_custom(const char *ieee, const char *cluster,
                                 const char *attr, const char *value, bool is_string,
                                 int64_t num_value)
{
    const cfg_general_t *gen = cfg_get_general();
    const char *header = gen->header_mqtt;
    const char *tmpl = gen->custom_mqtt_json;

    if (tmpl[0] == '\0') {
        ESP_LOGW(TAG, "Custom MQTT template is empty");
        return ESP_ERR_INVALID_ARG;
    }

    // Construire le topic
    // Topic : l'en-tete seul, comme la LiXee-Box (l'IEEE est dans la charge utile)
    char topic[256];
    snprintf(topic, sizeof(topic), "%s", header);
    (void)ieee;

    // Remplacer les placeholders dans le template
    // Allouer un buffer suffisamment grand
    char payload[512];
    const char *src = tmpl;
    char *dst = payload;
    char *dst_end = payload + sizeof(payload) - 1;

    while (*src && dst < dst_end) {
        if (*src == '<') {
            if (strncmp(src, "<IEEE>", 6) == 0) {
                if (!append_fmt(&dst, dst_end, "%s", ieee)) break;
                src += 6; continue;
            }
            if (strncmp(src, "<cluster>", 9) == 0) {
                if (!append_fmt(&dst, dst_end, "%s", cluster)) break;
                src += 9; continue;
            }
            if (strncmp(src, "<attribute>", 11) == 0) {
                if (!append_fmt(&dst, dst_end, "%s", attr)) break;
                src += 11; continue;
            }
            if (strncmp(src, "<value>", 7) == 0) {
                bool ok = is_string
                    ? append_fmt(&dst, dst_end, "\"%s\"", value)
                    : append_fmt(&dst, dst_end, "%lld", (long long)num_value);
                if (!ok) break;
                src += 7; continue;
            }
        }
        *dst++ = *src++;
    }
    *dst = '\0';

    return mqtt_publish(topic, payload, 1, false);   // QoS 1 (LiXee-Box)
}

// ============================================================================
// Commandes recues : "lixee/cmd/<IEEE>/action", charge = nom d'une action du
// template (boutons et interrupteurs crees par la decouverte Home Assistant).
// Gestionnaire par defaut, utilise tant qu'aucun autre n'est enregistre par
// mqtt_manager_register_cmd_handler() : la souscription existait, mais rien
// ne traitait les messages.
// ============================================================================

static void builtin_cmd_handler(const char *ieee_str, const char *cmd_type, const char *payload)
{
    // lixee/cmd/<IEEE>/action ou /switch (LiXee-Box) : action du template par son nom
    if (strcmp(cmd_type, "action") != 0 && strcmp(cmd_type, "switch") != 0) {
        ESP_LOGW(TAG, "Commande MQTT '%s' non geree", cmd_type);
        return;
    }
    uint64_t ieee = strtoull(ieee_str, NULL, 16);
    const zigate_device_t *dev = zigate_devices_find_by_ieee(ieee);
    const devdata_template_t *t = device_data_get_template(ieee);
    if (!dev || !t) {
        ESP_LOGW(TAG, "Commande MQTT pour un appareil inconnu ou sans template : %s", ieee_str);
        return;
    }
    // Charge eventuellement entre guillemets
    char name[DEVDATA_NAME_LEN];
    strlcpy(name, payload[0] == '"' ? payload + 1 : payload, sizeof(name));
    size_t n = strlen(name);
    while (n && (name[n - 1] == '"' || name[n - 1] == ' ' || name[n - 1] == '\n' || name[n - 1] == '\r')) name[--n] = '\0';

    for (int i = 0; i < t->action_count; i++) {
        const devdata_template_action_t *a = &t->actions[i];
        if (strcasecmp(a->name, name) != 0) continue;
        // Position de volet : la valeur du template n'est pas une position
        // voulue, l'envoyer deplacerait le volet de facon inattendue (refuse
        // aussi par la LiXee-Box)
        if (zigate_action_is_cover_position(a->command)) {
            ESP_LOGW(TAG, "Action MQTT '%s' (position de volet) refusee : position non fournie", name);
            return;
        }
        esp_err_t err = zigate_action_send(a->command, dev->short_addr,
                                           a->endpoint ? a->endpoint : dev->endpoint,
                                           a->value, a->cluster, a->manufacturer_code);
        ESP_LOGI(TAG, "Action MQTT '%s' -> %s : %s", name, ieee_str, esp_err_to_name(err));
        return;
    }
    ESP_LOGW(TAG, "Action MQTT '%s' absente du template de %s", name, ieee_str);
}

// ============================================================================
// Decouverte Home Assistant
// ============================================================================

static esp_err_t publish_json_retained(const char *topic, cJSON *root, int *count)
{
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;
    esp_err_t err = mqtt_publish(topic, json, 0, true);
    free(json);
    if (err == ESP_OK && count) (*count)++;
    return err;
}

/** Bloc "device" commun : memes identifiants que la LiXee-Box. */
static void add_ha_device(cJSON *root, const zigate_device_t *dev, const char *ieee)
{
    char name[96], ident[96];
    snprintf(name, sizeof(name), "LiXee-GW_%s_%s", dev->model, ieee);
    snprintf(ident, sizeof(ident), "LiXee-GW%s_%s", dev->model, ieee);
    cJSON *d = cJSON_AddObjectToObject(root, "device");
    if (!d) return;
    cJSON_AddStringToObject(d, "name", dev->alias[0] ? dev->alias : name);
    cJSON_AddStringToObject(d, "model", dev->model[0] ? dev->model : "Zigbee");
    cJSON_AddStringToObject(d, "manufacturer", dev->manufacturer[0] ? dev->manufacturer : "LiXee");
    cJSON *ids = cJSON_AddArrayToObject(d, "identifiers");
    if (ids) cJSON_AddItemToArray(ids, cJSON_CreateString(ident));
}

/** Texte utilisable comme object_id d'un topic de decouverte ([A-Za-z0-9_-]). */
static void topic_safe(const char *in, char *out, size_t len)
{
    size_t o = 0;
    for (; *in && o + 1 < len; in++) {
        char c = *in;
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-';
        out[o++] = ok ? c : '_';
    }
    out[o] = '\0';
}

/** Suffixe insensible a la casse : rend la longueur du prefixe, 0 sinon. */
static size_t name_prefix(const char *name, const char *suffix)
{
    size_t n = strlen(name), s = strlen(suffix);
    if (n <= s || strcasecmp(name + n - s, suffix) != 0) return 0;
    return n - s;
}

static const devdata_template_action_t *find_visible_action(const devdata_template_t *t,
                                                            const char *name)
{
    for (int i = 0; i < t->action_count; i++) {
        if (t->actions[i].visible && strcasecmp(t->actions[i].name, name) == 0) return &t->actions[i];
    }
    return NULL;
}

/** Etiquette MQTT d'un attribut : "0" ou "0_ep2" pour une ligne propre a un endpoint (epAttrLabel). */
static void attr_label(uint16_t key, uint8_t ep, char *out, size_t len)
{
    if (ep) snprintf(out, len, "%u_ep%u", (unsigned)(key & 0x0FFF), (unsigned)ep);
    else snprintf(out, len, "%u", (unsigned)key);
}

static esp_err_t publish_switch(const zigate_device_t *dev, const char *ieee, const char *label,
                                const char *uid_suffix, const char *on, const char *off, int *count,
                                const char *state_lbl = "0")
{
    const cfg_general_t *gen = cfg_get_general();
    char buf[160], topic[256], safe[64];
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    snprintf(buf, sizeof(buf), "%s %s", dev->model[0] ? dev->model : ieee, label);
    cJSON_AddStringToObject(root, "name", buf);
    snprintf(buf, sizeof(buf), "%s_switch%s", ieee, uid_suffix);
    cJSON_AddStringToObject(root, "unique_id", buf);
    cJSON_AddStringToObject(root, "icon", "mdi:power-plug");
    snprintf(topic, sizeof(topic), "%s%s_6_%s/state", gen->header_mqtt, ieee, state_lbl);
    cJSON_AddStringToObject(root, "state_topic", topic);
    snprintf(buf, sizeof(buf), "{{ value_json.value_6_%s }}", state_lbl);
    cJSON_AddStringToObject(root, "value_template", buf);
    snprintf(buf, sizeof(buf), "lixee/cmd/%s/action", ieee);
    cJSON_AddStringToObject(root, "command_topic", buf);
    cJSON_AddStringToObject(root, "payload_on", on);
    cJSON_AddStringToObject(root, "payload_off", off);
    cJSON_AddStringToObject(root, "state_on", "1");
    cJSON_AddStringToObject(root, "state_off", "0");
    add_ha_device(root, dev, ieee);
    topic_safe(uid_suffix, safe, sizeof(safe));
    snprintf(topic, sizeof(topic), "homeassistant/switch/%s_switch%s/config", ieee, safe);
    return publish_json_retained(topic, root, count);
}

static esp_err_t publish_discovery_impl(const zigate_device_t *dev, uint64_t ieee_addr, int *out_count);

esp_err_t mqtt_manager_publish_discovery(uint64_t ieee_addr, int *out_count)
{
    if (out_count) *out_count = 0;
    return publish_discovery_impl(zigate_devices_find_by_ieee(ieee_addr), ieee_addr, out_count);
}

esp_err_t mqtt_manager_publish_discovery_info(uint64_t ieee_addr, const char *model, const char *alias,
                                              const char *manufacturer, int *out_count)
{
    if (out_count) *out_count = 0;
    // Fiche minimale : la publication n'utilise que modele, alias et fabricant
    zigate_device_t dev = {};
    dev.ieee_addr = ieee_addr;
    strlcpy(dev.model, model ? model : "", sizeof(dev.model));
    strlcpy(dev.alias, alias ? alias : "", sizeof(dev.alias));
    strlcpy(dev.manufacturer, manufacturer ? manufacturer : "", sizeof(dev.manufacturer));
    return publish_discovery_impl(&dev, ieee_addr, out_count);
}

static esp_err_t publish_discovery_impl(const zigate_device_t *dev, uint64_t ieee_addr, int *out_count)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    const devdata_template_t *t = device_data_get_template(ieee_addr);
    if (!dev || !t || !t->loaded) return ESP_ERR_NOT_FOUND;

    const cfg_general_t *gen = cfg_get_general();
    char ieee[20];
    mqtt_ieee_str(ieee_addr, ieee, sizeof(ieee));

    // Filtre par mode TIC (ZLinky uniquement), comme la LiXee-Box
    int mode = -1;
    if (strncmp(dev->model, "ZLinky", 6) == 0) {
        mode = device_data_linky_mode(ieee_addr);
        if (mode < 0) mode = gen->linky_mode;
    }

    int count = 0;
    esp_err_t last_err = ESP_OK;

    // --- Capteurs : etats ayant une icone MQTT ---
    for (int i = 0; i < t->state_count; i++) {
        const devdata_template_state_t *s = &t->states[i];
        if (!s->mqtt_icon[0] || !device_data_state_in_mode(s, mode)) continue;

        char key[32], buf[160], topic[256], al[16];
        attr_label(s->attribute, s->endpoint, al, sizeof(al));
        snprintf(key, sizeof(key), "value_%u_%s", (unsigned)s->cluster, al);

        cJSON *root = cJSON_CreateObject();
        if (!root) return ESP_ERR_NO_MEM;
        cJSON_AddStringToObject(root, "name", s->name);
        // unique_id et topic de config en cluster DECIMAL, comme la LiXee-Box
        snprintf(buf, sizeof(buf), "%s_%u_%s", ieee, (unsigned)s->cluster, al);
        cJSON_AddStringToObject(root, "unique_id", buf);
        if (s->mqtt_device_class[0] && strcmp(s->mqtt_device_class, "null") != 0)
            cJSON_AddStringToObject(root, "device_class", s->mqtt_device_class);
        else
            cJSON_AddNullToObject(root, "device_class");
        if (s->mqtt_state_class[0] && strcmp(s->mqtt_state_class, "null") != 0)
            cJSON_AddStringToObject(root, "state_class", s->mqtt_state_class);
        else
            cJSON_AddNullToObject(root, "state_class");
        if (s->unit[0]) cJSON_AddStringToObject(root, "unit_of_measurement", s->unit);
        snprintf(buf, sizeof(buf), "mdi:%s", s->mqtt_icon);
        cJSON_AddStringToObject(root, "icon", buf);
        // Topic d'etat : celui de publish_ha() (cluster en decimal, LiXee-Box)
        snprintf(topic, sizeof(topic), "%s%s_%u_%s/state", gen->header_mqtt, ieee,
                 (unsigned)s->cluster, al);
        cJSON_AddStringToObject(root, "state_topic", topic);
        bool num = strcmp(s->type, "numeric") == 0 || strcmp(s->type, "float") == 0;
        if (num && s->coefficient != 1.0f) {
            snprintf(buf, sizeof(buf), "{{ value_json.%s | float * %g }}", key, (double)s->coefficient);
        } else {
            snprintf(buf, sizeof(buf), "{{ value_json.%s }}", key);
        }
        cJSON_AddStringToObject(root, "value_template", buf);
        add_ha_device(root, dev, ieee);

        snprintf(topic, sizeof(topic), "%s%s_%u_%s/config", gen->header_mqtt, ieee,
                 (unsigned)s->cluster, al);
        esp_err_t err = publish_json_retained(topic, root, &count);
        if (err != ESP_OK) last_err = err;
        vTaskDelay(pdMS_TO_TICKS(20));       // ne pas saturer la file du client
    }

    // --- Interrupteurs et boutons : actions visibles ---
    const devdata_template_action_t *on  = find_visible_action(t, "ON");
    const devdata_template_action_t *off = find_visible_action(t, "OFF");
    if (on && off) {
        esp_err_t err = publish_switch(dev, ieee, "Switch", "", on->name, off->name, &count);
        if (err != ESP_OK) last_err = err;
    }

    for (int i = 0; i < t->action_count; i++) {
        const devdata_template_action_t *a = &t->actions[i];
        if (!a->visible) continue;

        // Paire <nom>_ON / <nom>_OFF : un interrupteur, lie a l'etat 0006/0 de
        // l'endpoint de l'action si le template declare cette ligne, sinon a 0006/0
        char prefix[DEVDATA_NAME_LEN], other[DEVDATA_NAME_LEN + 4];
        size_t pl = name_prefix(a->name, "_ON");
        if (pl) {
            memcpy(prefix, a->name, pl);
            prefix[pl] = '\0';
            snprintf(other, sizeof(other), "%s_OFF", prefix);
            const devdata_template_action_t *b = find_visible_action(t, other);
            if (b) {
                char suffix[DEVDATA_NAME_LEN + 2], lbl[16] = "0";
                snprintf(suffix, sizeof(suffix), "_%s", prefix);
                uint16_t sk = devdata_ep_key(0, a->endpoint);
                for (int k = 0; sk != 0 && k < t->state_count; k++) {
                    if (t->states[k].cluster == 6 && t->states[k].attribute == sk) {
                        attr_label(sk, a->endpoint, lbl, sizeof(lbl));
                        break;
                    }
                }
                esp_err_t err = publish_switch(dev, ieee, prefix, suffix, a->name, b->name, &count, lbl);
                if (err != ESP_OK) last_err = err;
                continue;
            }
        }
        pl = name_prefix(a->name, "_OFF");
        if (pl) {
            memcpy(prefix, a->name, pl);
            prefix[pl] = '\0';
            snprintf(other, sizeof(other), "%s_ON", prefix);
            if (find_visible_action(t, other)) continue;   // deja publie avec son _ON
        }
        if (on && off && (a == on || a == off)) continue;   // deja dans l'interrupteur

        char buf[160], topic[256], safe[DEVDATA_NAME_LEN];
        cJSON *root = cJSON_CreateObject();
        if (!root) return ESP_ERR_NO_MEM;
        cJSON_AddStringToObject(root, "name", a->name);
        snprintf(buf, sizeof(buf), "%s_action_%s", ieee, a->name);
        cJSON_AddStringToObject(root, "unique_id", buf);
        snprintf(buf, sizeof(buf), "lixee/cmd/%s/action", ieee);
        cJSON_AddStringToObject(root, "command_topic", buf);
        cJSON_AddStringToObject(root, "payload_press", a->name);
        add_ha_device(root, dev, ieee);
        // object_id limite a [A-Za-z0-9_-] : la LiXee-Box mettait le nom brut
        // ("HORS GEL"), topic que Home Assistant ignore
        topic_safe(a->name, safe, sizeof(safe));
        snprintf(topic, sizeof(topic), "homeassistant/button/%s_%s/config", ieee, safe);
        esp_err_t err = publish_json_retained(topic, root, &count);
        if (err != ESP_OK) last_err = err;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // --- Entites tarif du Linky (ZLinky configure dans Config Energie) ---
    if (strncmp(dev->model, "ZLinky", 6) == 0 && strtoull(gen->zlinky, NULL, 16) == ieee_addr) {
        static const struct { const char *key, *name, *icon; } T[3] = {
            { "tarif_actuel",   "Tarif actuel",      "mdi:currency-eur" },
            { "couleur_jour",   "Couleur du jour",   "mdi:palette" },
            { "couleur_demain", "Couleur de demain", "mdi:calendar-today" },
        };
        for (int i = 0; i < 3; i++) {
            char buf[160], topic[256];
            cJSON *root = cJSON_CreateObject();
            if (!root) return ESP_ERR_NO_MEM;
            cJSON_AddStringToObject(root, "name", T[i].name);
            snprintf(buf, sizeof(buf), "%s_%s", ieee, T[i].key);
            cJSON_AddStringToObject(root, "unique_id", buf);
            cJSON_AddStringToObject(root, "icon", T[i].icon);
            snprintf(topic, sizeof(topic), "%s%s_%s/state", gen->header_mqtt, ieee, T[i].key);
            cJSON_AddStringToObject(root, "state_topic", topic);
            snprintf(buf, sizeof(buf), "{{ value_json.%s }}", T[i].key);
            cJSON_AddStringToObject(root, "value_template", buf);
            add_ha_device(root, dev, ieee);
            snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_%s/config", ieee, T[i].key);
            esp_err_t err = publish_json_retained(topic, root, &count);
            if (err != ESP_OK) last_err = err;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    if (out_count) *out_count = count;
    ESP_LOGI(TAG, "Decouverte HA %s : %d message(s)", ieee, count);
    return count > 0 ? ESP_OK : (last_err != ESP_OK ? last_err : ESP_ERR_NOT_FOUND);
}

// ============================================================================
// Callbacks esp_mqtt_client
// ============================================================================

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        s_reconnect_attempts = 0;
        s_publish_errors = 0;
        ESP_LOGI(TAG, "Connected to MQTT broker");

        // Souscrire aux commandes
        esp_mqtt_client_subscribe(s_client, "lixee/cmd/#", 0);
        ESP_LOGI(TAG, "Subscribed to lixee/cmd/#");
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected from MQTT broker");
        break;

    case MQTT_EVENT_DATA:
        // Message recu sur un topic souscrit
        if (event->topic_len > 0 && event->data_len > 0) {
            mqtt_cmd_handler_t handler = s_cmd_handler ? s_cmd_handler : builtin_cmd_handler;
            // Parser le topic: "lixee/cmd/{IEEE}/{command}"
            char topic[128];
            int tlen = (event->topic_len < (int)sizeof(topic) - 1) ? event->topic_len : (int)sizeof(topic) - 1;
            memcpy(topic, event->topic, tlen);
            topic[tlen] = '\0';

            char payload[256];
            int plen = (event->data_len < (int)sizeof(payload) - 1) ? event->data_len : (int)sizeof(payload) - 1;
            memcpy(payload, event->data, plen);
            payload[plen] = '\0';

            // Extraire IEEE et commande du topic
            // Format: lixee/cmd/<IEEE>/<cmd_type>
            const char *prefix = "lixee/cmd/";
            if (strncmp(topic, prefix, strlen(prefix)) == 0) {
                const char *rest = topic + strlen(prefix);
                const char *slash = strchr(rest, '/');
                if (slash) {
                    char ieee_str[20];
                    int ieee_len = (int)(slash - rest);
                    if (ieee_len > 0 && ieee_len < (int)sizeof(ieee_str)) {
                        memcpy(ieee_str, rest, ieee_len);
                        ieee_str[ieee_len] = '\0';
                        const char *cmd_type = slash + 1;
                        ESP_LOGI(TAG, "MQTT cmd: ieee=%s cmd=%s payload=%s",
                                 ieee_str, cmd_type, payload);
                        handler(ieee_str, cmd_type, payload);
                    }
                }
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error event");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGW(TAG, "  TLS error: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGW(TAG, "  Socket errno: %d", event->error_handle->esp_transport_sock_errno);
        }
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGD(TAG, "Subscribed, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "Published, msg_id=%d", event->msg_id);
        break;

    default:
        ESP_LOGD(TAG, "MQTT event: %d", (int)event_id);
        break;
    }
}

// ============================================================================
// API publique
// ============================================================================

esp_err_t mqtt_manager_init(void)
{
    if (s_initialized) return ESP_OK;

    const cfg_general_t *gen = cfg_get_general();
    const cfg_settings_t *settings = cfg_get_settings();

    if (!settings->enable_mqtt) {
        ESP_LOGI(TAG, "MQTT disabled in config");
        s_initialized = true;
        return ESP_OK;
    }

    if (gen->serv_mqtt[0] == '\0') {
        ESP_LOGW(TAG, "MQTT server not configured");
        s_initialized = true;
        return ESP_OK;
    }

    // Construire l'URI: "mqtt://host:port"
    int port = 1883;
    if (gen->port_mqtt[0] != '\0') {
        port = atoi(gen->port_mqtt);
        if (port <= 0) port = 1883;
    }
    snprintf(s_uri, sizeof(s_uri), "mqtt://%s:%d", gen->serv_mqtt, port);
    ESP_LOGI(TAG, "MQTT URI: %s", s_uri);

    // Configurer le client
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = s_uri;
    mqtt_cfg.session.keepalive = 120;
    mqtt_cfg.network.disable_auto_reconnect = true; // On gere la reconnexion nous-memes
    mqtt_cfg.buffer.size = 1024;
    // Correction : outbox sans limite. Toute publication passe par
    // esp_mqtt_client_enqueue() et reste dans l'outbox (RAM interne : pas de
    // CONFIG_MQTT_OUTBOX_DATA_ON_EXTERNAL_MEMORY) tant qu'elle n'est pas
    // emise ; un blocage TCP (broker injoignable sans deconnexion detectee)
    // la faisait grossir jusqu'a epuiser la RAM interne. Au-dela de 16 Ko,
    // enqueue refuse (-2) : la publication est comptee en erreur et perdue,
    // le pont continue de fonctionner. 16 Ko couvrent une rafale de
    // decouverte Home Assistant (messages de ~0,5-1 Ko, cadences a 20 ms)
    // et tiennent dans la marge de RAM interne du projet (~50 Ko libres).
    mqtt_cfg.outbox.limit = 16 * 1024;

    if (gen->client_id_mqtt[0] != '\0') {
        mqtt_cfg.credentials.client_id = gen->client_id_mqtt;
    }
    if (gen->user_mqtt[0] != '\0') {
        mqtt_cfg.credentials.username = gen->user_mqtt;
        if (gen->pass_mqtt[0] != '\0') {
            mqtt_cfg.credentials.authentication.password = gen->pass_mqtt;
        }
    }

    if (!s_client_mtx) s_client_mtx = xSemaphoreCreateMutex();
    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);

    s_initialized = true;
    ESP_LOGI(TAG, "MQTT client initialized");

    // Log le mode actif
    if (gen->ha_mqtt) ESP_LOGI(TAG, "Mode: Home Assistant");
    if (gen->tb_mqtt) ESP_LOGI(TAG, "Mode: ThingsBoard");
    if (gen->custom_mqtt) ESP_LOGI(TAG, "Mode: Custom");

    return ESP_OK;
}

esp_err_t mqtt_manager_start(void)
{
    if (!s_initialized || !s_client) {
        ESP_LOGD(TAG, "MQTT not initialized or disabled, skip start");
        return ESP_OK;
    }

    if (s_started) return ESP_OK;

    esp_err_t ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        return ret;
    }

    s_started = true;
    ESP_LOGI(TAG, "MQTT client started");
    return ESP_OK;
}

esp_err_t mqtt_manager_stop(void)
{
    if (!s_client || !s_started) return ESP_OK;

    esp_mqtt_client_stop(s_client);
    s_started = false;
    s_connected = false;
    ESP_LOGI(TAG, "MQTT client stopped");
    return ESP_OK;
}

bool mqtt_manager_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_manager_publish_value(const char *ieee_addr,
                                      const char *cluster,
                                      const char *attribute,
                                      const char *value,
                                      bool is_string,
                                      int64_t num_value)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    const cfg_general_t *gen = cfg_get_general();

    esp_err_t ret = ESP_OK;

    if (gen->ha_mqtt) {
        ret = publish_ha(ieee_addr, cluster, attribute, value, is_string, num_value);
    }
    if (gen->tb_mqtt) {
        esp_err_t r = publish_tb(ieee_addr, cluster, attribute, value, is_string, num_value);
        if (r != ESP_OK) ret = r;
    }
    if (gen->custom_mqtt) {
        esp_err_t r = publish_custom(ieee_addr, cluster, attribute, value, is_string, num_value);
        if (r != ESP_OK) ret = r;
    }
    // Aucun mode choisi : rien n'est publie, comme la LiXee-Box

    return ret;
}

void mqtt_manager_publish_linky_tariff(uint64_t ieee_addr, const char *tarif, int today, int tomorrow)
{
    static char s_tarif[32] = "", s_jour[8] = "", s_demain[8] = "";   // jamais publie
    const cfg_general_t *gen = cfg_get_general();
    if (!s_connected || !gen->ha_mqtt || !tarif || !tarif[0] || !strcmp(tarif, "---")) return;
    static const char *const COLOR[4] = { "", "BLEU", "BLANC", "ROUGE" };
    const char *jour = (today >= 1 && today <= 3) ? COLOR[today] : "";
    const char *demain = (tomorrow >= 1 && tomorrow <= 3) ? COLOR[tomorrow] : "";
    char ieee[20], topic[160], payload[96];
    mqtt_ieee_str(ieee_addr, ieee, sizeof(ieee));
    struct { const char *key, *val; char *last; size_t len; } e[3] = {
        { "tarif_actuel", tarif, s_tarif, sizeof(s_tarif) },
        { "couleur_jour", jour, s_jour, sizeof(s_jour) },
        { "couleur_demain", demain, s_demain, sizeof(s_demain) },
    };
    for (int i = 0; i < 3; i++) {
        if (!strcmp(e[i].last, e[i].val)) continue;
        snprintf(topic, sizeof(topic), "%s%s_%s/state", gen->header_mqtt, ieee, e[i].key);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, e[i].key, e[i].val);
        char *js = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        if (!js) continue;
        strlcpy(payload, js, sizeof(payload));
        free(js);
        if (mqtt_publish(topic, payload, 0, true) == ESP_OK) strlcpy(e[i].last, e[i].val, e[i].len);
    }
}

esp_err_t mqtt_manager_publish_attr(uint64_t ieee_addr, uint16_t cluster, uint16_t attr,
                                    const char *hex, uint8_t zcl_type, bool has_num, int64_t num)
{
    char ieee[20], cl[8], at[16];
    mqtt_ieee_str(ieee_addr, ieee, sizeof(ieee));
    snprintf(cl, sizeof(cl), "%u", (unsigned)cluster);       // decimal, comme la LiXee-Box
    uint8_t kep = 0;
    uint16_t zat = device_data_resolve_key(ieee_addr, cluster, attr, &kep);
    attr_label(attr, kep, at, sizeof(at));                   // "0_ep2" : ligne d'un endpoint
    // Chaines ZCL (et Basic sans valeur numerique) : texte decode, pas l'hexa
    bool is_str = (zcl_type >= 0x41 && zcl_type <= 0x44) || (cluster == 0x0000 && !has_num);
    if (is_str) {
        char txt[96];
        device_data_format_value(cluster, zat, hex, zcl_type, "", 1.0f, txt, sizeof(txt));
        return mqtt_manager_publish_value(ieee, cl, at, txt, true, 0);
    }
    return mqtt_manager_publish_value(ieee, cl, at, hex, false, has_num ? num : 0);
}

esp_err_t mqtt_manager_publish_device(uint64_t ieee_addr)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    device_data_t *dd = device_data_find(ieee_addr);
    if (!dd) return ESP_ERR_NOT_FOUND;

    char ieee_str[20];
    mqtt_ieee_str(ieee_addr, ieee_str, sizeof(ieee_str));

    int published = 0;
    for (int i = 0; i < dd->cluster_count; i++) {
        devdata_cluster_t *cl = &dd->clusters[i];

        for (int j = 0; j < cl->attr_count; j++) {
            devdata_attr_t *attr = &cl->attrs[j];
            if (attr->value[0] == '\0') continue;

            int64_t v = 0;
            bool has_num = device_data_hex_to_int64(attr->value, attr->type, cl->cluster_id,
                                                    attr->attr_id, &v);
            mqtt_manager_publish_attr(ieee_addr, cl->cluster_id, attr->attr_id, attr->value,
                                      attr->type, has_num, v);
            published++;

            // Petit delai pour ne pas saturer le buffer MQTT
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (published > 0) {
        ESP_LOGD(TAG, "Published %d values for device %s", published, ieee_str);
    }

    return ESP_OK;
}

void mqtt_manager_register_cmd_handler(mqtt_cmd_handler_t handler)
{
    s_cmd_handler = handler;
}

static volatile bool s_reload_req;

void mqtt_manager_request_reload(void) { s_reload_req = true; }

/** Nouvelle configuration : client arrete, detruit puis recree (boucle MQTT). */
static void reload_client(void)
{
    s_reload_req = false;
    ESP_LOGI(TAG, "Nouvelle configuration MQTT : reconnexion");
    s_connected = false;                     // plus de publication sur l'ancien client
    if (s_client) {
        if (s_started) esp_mqtt_client_stop(s_client);
        vTaskDelay(pdMS_TO_TICKS(100));
        if (s_client_mtx) xSemaphoreTake(s_client_mtx, portMAX_DELAY);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        if (s_client_mtx) xSemaphoreGive(s_client_mtx);
    }
    s_started = false;
    s_initialized = false;
    mqtt_manager_init();
    mqtt_manager_start();
}

void mqtt_manager_loop(void)
{
    if (s_reload_req) reload_client();
    if (!s_initialized || !s_client || !s_started) return;

    const cfg_settings_t *settings = cfg_get_settings();
    if (!settings->enable_mqtt) return;

    uint32_t now = now_ms();

    // --- Health Check (heartbeat toutes les 30s) ---
    uint32_t elapsed_s = (now - s_last_health_check_s * 1000) / 1000;
    if (s_connected && elapsed_s >= MQTT_HEALTH_CHECK_INTERVAL_S) {
        s_last_health_check_s = now / 1000;

        const cfg_general_t *gen = cfg_get_general();
        char topic[256];
        snprintf(topic, sizeof(topic), "%sheartbeat", gen->header_mqtt);

        // Payload avec timestamp
        time_t epoch = ntp_sync_get_epoch();
        char payload[64];
        snprintf(payload, sizeof(payload), "{\"timestamp\":%ld}", (long)epoch);

        esp_err_t ret = mqtt_publish(topic, payload, 0, false);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Heartbeat publish failed (errors=%d)", s_publish_errors);
        }

        // Trop d'erreurs -> forcer deconnexion pour reconnecter proprement
        if (s_publish_errors >= MQTT_MAX_PUBLISH_ERRORS) {
            ESP_LOGW(TAG, "Too many publish errors (%d), forcing disconnect",
                     s_publish_errors);
            esp_mqtt_client_disconnect(s_client);
            s_connected = false;
            s_publish_errors = 0;
        }
    }

    // --- Reconnexion ---
    if (!s_connected) {
        // Calculer le delai avec backoff
        uint32_t delay = MQTT_RECONNECT_DELAY_MS;
        if (s_reconnect_attempts >= MQTT_MAX_RECONNECT_ATTEMPTS) {
            delay = MQTT_RECONNECT_MAX_DELAY_MS;
        }

        if (now - s_last_reconnect_ms >= delay) {
            s_last_reconnect_ms = now;
            s_reconnect_attempts++;

            ESP_LOGI(TAG, "Reconnecting to MQTT (attempt %d)...", s_reconnect_attempts);
            esp_mqtt_client_reconnect(s_client);
        }
    }
}

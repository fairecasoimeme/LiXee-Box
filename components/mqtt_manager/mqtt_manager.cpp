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
#include "esp_crt_bundle.h"
#include "esp_tls_errors.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/platform_util.h"
#include "esp_random.h"
#include <sys/stat.h>
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

// Certificat de l'autorite privee (tlsMQTT = 2) : le client MQTT garde le
// pointeur, la memoire n'est liberee qu'apres esp_mqtt_client_destroy()
static char *s_ca_pem = NULL;
// Certificat client et cle privee (certMQTT, TLS mutuel), meme regle
static char *s_client_cert = NULL;
static char *s_client_key = NULL;
// Protocole ALPN (alpnMQTT) : liste terminee par NULL attendue par esp-tls
static char s_alpn_buf[32] = {};
static const char *s_alpn_protos[2] = { s_alpn_buf, NULL };

// Derniere erreur de connexion, affichee par la page MQTT (tache MQTT ->
// serveur web)
static char s_last_error[96] = {};
static portMUX_TYPE s_err_mux = portMUX_INITIALIZER_UNLOCKED;

static void set_last_error(const char *fmt, ...)
{
    char tmp[sizeof(s_last_error)];
    tmp[0] = '\0';
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(tmp, sizeof(tmp), fmt, ap);
        va_end(ap);
    }
    portENTER_CRITICAL(&s_err_mux);
    memcpy(s_last_error, tmp, sizeof(s_last_error));
    portEXIT_CRITICAL(&s_err_mux);
}

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
        set_last_error(NULL);
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

    case MQTT_EVENT_ERROR: {
        const esp_mqtt_error_codes_t *e = event->error_handle;
        ESP_LOGW(TAG, "MQTT error event");
        if (e->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            // Erreur mbedTLS : esp-tls la remonte selon les cas en positif ou
            // en negatif ; comparee ici en valeur absolue (-0x2700 = refus
            // du certificat, -0x0050 = connexion coupee par le serveur)
            const int tls_err = e->esp_tls_stack_err < 0 ? -e->esp_tls_stack_err : e->esp_tls_stack_err;
            ESP_LOGW(TAG, "  TLS error: 0x%x, mbedTLS: -0x%04X, verify flags: 0x%x",
                     e->esp_tls_last_esp_err, (unsigned)tls_err, e->esp_tls_cert_verify_flags);
            ESP_LOGW(TAG, "  Socket errno: %d", e->esp_transport_sock_errno);
            // Flags mbedTLS (MBEDTLS_X509_BADCERT_*) : 0x01 expire,
            // 0x04 nom du serveur different, 0x08 autorite inconnue. Souvent
            // nuls : le refus n'apparait alors que dans l'erreur mbedTLS.
            if (e->esp_tls_cert_verify_flags)
                set_last_error("Certificat du broker refusé (flags 0x%x)", e->esp_tls_cert_verify_flags);
            else if (tls_err == -MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
                set_last_error("Certificat du broker refusé (autorité, nom du serveur ou date)");
            else if (tls_err == -MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE)
                set_last_error("Connexion TLS refusée par le broker (certificat client absent ou refusé ?)");
            // Connexion coupee pendant la negociation : un broker (mosquitto,
            // OpenSSL) qui refuse le certificat client, ou qui en exige un,
            // ferme sans alerte, comme un port qui ne parle pas TLS
            else if (tls_err == -MBEDTLS_ERR_NET_CONN_RESET && s_client_cert)
                set_last_error("Connexion TLS coupée par le broker (certificat client refusé ?)");
            else if (tls_err == -MBEDTLS_ERR_NET_CONN_RESET)
                set_last_error("Connexion TLS coupée par le broker (port non TLS, ou certificat client exigé ?)");
            else if (e->esp_tls_last_esp_err == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED)
                set_last_error("Négociation TLS impossible (port TLS du broker ?) mbedTLS -0x%04X",
                               (unsigned)tls_err);
            // esp-tls porte aussi le TCP simple : erreurs reseau en clair
            else if (e->esp_tls_last_esp_err == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME)
                set_last_error("Nom du broker introuvable (DNS)");
            else if (e->esp_tls_last_esp_err == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT)
                set_last_error("Broker injoignable (délai dépassé)");
            else if (e->esp_tls_last_esp_err == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST)
                set_last_error("Broker injoignable (connexion refusée sur ce port)");
            else if (e->esp_tls_last_esp_err)
                set_last_error("Erreur de connexion %s", esp_err_to_name(e->esp_tls_last_esp_err));
            else if (e->esp_transport_sock_errno)
                set_last_error("Broker injoignable (errno %d)", e->esp_transport_sock_errno);
            else
                set_last_error("Connexion fermée par le serveur (est-ce un broker MQTT ?)");
        } else if (e->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGW(TAG, "  Connection refused: %d", e->connect_return_code);
            if (e->connect_return_code == MQTT_CONNECTION_REFUSE_BAD_USERNAME ||
                e->connect_return_code == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED)
                set_last_error("Identifiant ou mot de passe refusé par le broker");
            else
                set_last_error("Connexion refusée par le broker (code %d)", e->connect_return_code);
        }
        break;
    }

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
// Certificat de l'autorite privee (TLS)
// ============================================================================

/** Lit un fichier PEM enregistre (termine par un nul) ; NULL s'il n'y en a pas. */
static char *load_pem(const char *path, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && (size_t)n <= max && fseek(f, 0, SEEK_SET) == 0) {
            buf = (char *)malloc((size_t)n + 1);
            if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
                buf[n] = '\0';
            } else {
                free(buf);
                buf = NULL;
            }
        }
    }
    fclose(f);
    return buf;
}

static void free_secret(char *buf)
{
    if (!buf) return;
    mbedtls_platform_zeroize(buf, strlen(buf));
    free(buf);
}

/** Libere les PEM utilises par le client (apres esp_mqtt_client_destroy). */
static void free_tls_buffers(void)
{
    free(s_ca_pem);
    s_ca_pem = NULL;
    free(s_client_cert);
    s_client_cert = NULL;
    free_secret(s_client_key);
    s_client_key = NULL;
}

/** Ecrit un fichier en entier ; supprime en cas d'echec. */
static bool write_file(const char *path, const char *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    if (!ok) remove(path);
    return ok;
}

esp_err_t mqtt_manager_set_ca_pem(const char *pem, char *err, size_t err_len)
{
    if (err && err_len) err[0] = '\0';
    if (!pem || !pem[0]) {
        remove(CFG_PATH_MQTT_CA);
        return ESP_OK;
    }
    size_t n = strlen(pem);
    if (n > MQTT_CA_PEM_MAX) {
        if (err) snprintf(err, err_len, "Certificat trop volumineux (%u octets max)", (unsigned)MQTT_CA_PEM_MAX);
        return ESP_ERR_INVALID_SIZE;
    }
    if (!strstr(pem, "-----BEGIN CERTIFICATE-----")) {
        if (err) snprintf(err, err_len, "Format PEM attendu (-----BEGIN CERTIFICATE-----)");
        return ESP_ERR_INVALID_ARG;
    }
    // Analyse complete : le PEM doit etre termine par un nul compte dans la
    // longueur. r > 0 : nombre de certificats de la chaine rejetes.
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    int r = mbedtls_x509_crt_parse(&crt, (const unsigned char *)pem, n + 1);
    mbedtls_x509_crt_free(&crt);
    if (r != 0) {
        if (err) snprintf(err, err_len, "Certificat invalide (mbedTLS -0x%04X)", (unsigned)(r < 0 ? -r : r));
        return ESP_ERR_INVALID_ARG;
    }

    if (!write_file(CFG_PATH_MQTT_CA, pem, n)) {
        if (err) snprintf(err, err_len, "Ecriture impossible");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Certificat d'autorite MQTT enregistre (%u octets)", (unsigned)n);
    return ESP_OK;
}

bool mqtt_manager_ca_info(char *subject, size_t subj_len, char *not_after, size_t na_len)
{
    char *pem = load_pem(CFG_PATH_MQTT_CA, MQTT_CA_PEM_MAX);
    if (!pem) return false;
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    bool ok = mbedtls_x509_crt_parse(&crt, (const unsigned char *)pem, strlen(pem) + 1) >= 0
              && crt.raw.p != NULL;
    if (ok) {
        if (subject && subj_len && mbedtls_x509_dn_gets(subject, subj_len, &crt.subject) < 0)
            subject[0] = '\0';
        if (not_after && na_len)
            snprintf(not_after, na_len, "%04d-%02d-%02d",
                     crt.valid_to.year, crt.valid_to.mon, crt.valid_to.day);
    }
    mbedtls_x509_crt_free(&crt);
    free(pem);
    return ok;
}

// ============================================================================
// Certificat client (TLS mutuel)
// ============================================================================

static int rng_fill(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    esp_fill_random(buf, len);
    return 0;
}

/**
 * Bloc "-----BEGIN ...PRIVATE KEY-----" ... "-----END ...-----" du texte :
 * debut et longueur, false s'il n'y en a pas. encrypted : cle PKCS#8 chiffree.
 */
static bool find_key_block(const char *pem, const char **start, size_t *len, bool *encrypted)
{
    const char *p = pem;
    while ((p = strstr(p, "-----BEGIN ")) != NULL) {
        const char *label = p + 11;
        const char *eol = strstr(label, "-----");
        if (!eol) return false;
        size_t lab = (size_t)(eol - label);
        if (lab >= 11 && memcmp(eol - 11, "PRIVATE KEY", 11) == 0) {
            const char *end = strstr(eol, "-----END ");
            const char *fin = end ? strstr(end + 9, "-----") : NULL;
            if (!fin) return false;
            *start = p;
            *len = (size_t)(fin + 5 - p);
            *encrypted = lab >= 9 && memcmp(label, "ENCRYPTED", 9) == 0;
            return true;
        }
        p = eol + 5;
    }
    return false;
}

/**
 * Copie les blocs CERTIFICATE du texte dans out, chacun suivi d'un saut de
 * ligne (taille >= strlen(pem) + strlen(pem) / 32 + 2 : un bloc fait plus
 * de 32 caracteres).
 */
static size_t extract_cert_blocks(const char *pem, char *out)
{
    static const char BEGIN[] = "-----BEGIN CERTIFICATE-----";
    static const char END[] = "-----END CERTIFICATE-----";
    size_t n = 0;
    const char *p = pem;
    while ((p = strstr(p, BEGIN)) != NULL) {
        const char *e = strstr(p, END);
        if (!e) break;
        size_t len = (size_t)(e + sizeof(END) - 1 - p);
        memcpy(out + n, p, len);
        n += len;
        out[n++] = '\n';
        p = e + sizeof(END) - 1;
    }
    out[n] = '\0';
    return n;
}

esp_err_t mqtt_manager_set_client_pem(const char *pem, char *err, size_t err_len)
{
    if (err && err_len) err[0] = '\0';
    if (!pem || !pem[0]) {
        remove(CFG_PATH_MQTT_CERT);
        remove(CFG_PATH_MQTT_KEY);
        return ESP_OK;
    }
    size_t n = strlen(pem);
    if (n > MQTT_CLIENT_PEM_MAX) {
        if (err) snprintf(err, err_len, "Fichier trop volumineux (%u octets max)", (unsigned)MQTT_CLIENT_PEM_MAX);
        return ESP_ERR_INVALID_SIZE;
    }

    const char *key_start = NULL;
    size_t key_len = 0;
    bool encrypted = false;
    bool has_key = find_key_block(pem, &key_start, &key_len, &encrypted);
    char *certs = (char *)malloc(n + n / 32 + 2);
    char *key = has_key ? (char *)malloc(key_len + 1) : NULL;
    if (!certs || (has_key && !key)) {
        free(certs);
        free(key);
        if (err) snprintf(err, err_len, "Mémoire insuffisante");
        return ESP_ERR_NO_MEM;
    }
    size_t certs_len = extract_cert_blocks(pem, certs);
    if (has_key) {
        memcpy(key, key_start, key_len);
        key[key_len] = '\0';
    }

    esp_err_t ret = ESP_OK;
    mbedtls_x509_crt crt;
    mbedtls_pk_context pk;
    mbedtls_x509_crt_init(&crt);
    mbedtls_pk_init(&pk);

    if (!certs_len && !has_key) {
        if (err) snprintf(err, err_len, "Format PEM attendu (certificat ou clé privée)");
        ret = ESP_ERR_INVALID_ARG;
    } else if (encrypted) {
        if (err) snprintf(err, err_len, "Clé privée chiffrée : l'exporter sans mot de passe");
        ret = ESP_ERR_INVALID_ARG;
    }
    if (ret == ESP_OK && has_key) {
        int r = mbedtls_pk_parse_key(&pk, (const unsigned char *)key, key_len + 1, NULL, 0, rng_fill, NULL);
        if (r == MBEDTLS_ERR_PK_PASSWORD_REQUIRED) {
            if (err) snprintf(err, err_len, "Clé privée chiffrée : l'exporter sans mot de passe");
            ret = ESP_ERR_INVALID_ARG;
        } else if (r != 0) {
            if (err) snprintf(err, err_len, "Clé privée invalide (mbedTLS -0x%04X)", (unsigned)-r);
            ret = ESP_ERR_INVALID_ARG;
        }
    }
    if (ret == ESP_OK && certs_len) {
        int r = mbedtls_x509_crt_parse(&crt, (const unsigned char *)certs, certs_len + 1);
        if (r != 0) {
            if (err) snprintf(err, err_len, "Certificat invalide (mbedTLS -0x%04X)", (unsigned)(r < 0 ? -r : r));
            ret = ESP_ERR_INVALID_ARG;
        }
    }
    // Certificat et cle fournis ensemble : ils doivent aller par paire
    if (ret == ESP_OK && certs_len && has_key &&
        mbedtls_pk_check_pair(&crt.pk, &pk, rng_fill, NULL) != 0) {
        if (err) snprintf(err, err_len, "La clé privée ne correspond pas au certificat");
        ret = ESP_ERR_INVALID_ARG;
    }
    mbedtls_x509_crt_free(&crt);
    mbedtls_pk_free(&pk);

    if (ret == ESP_OK && certs_len && !write_file(CFG_PATH_MQTT_CERT, certs, certs_len)) ret = ESP_FAIL;
    if (ret == ESP_OK && has_key && !write_file(CFG_PATH_MQTT_KEY, key, key_len)) ret = ESP_FAIL;
    if (ret == ESP_FAIL && err) snprintf(err, err_len, "Ecriture impossible");
    if (ret == ESP_OK)
        ESP_LOGI(TAG, "Certificat client MQTT enregistre (certificat %u octets, cle %s)",
                 (unsigned)certs_len, has_key ? "oui" : "non");
    free(certs);
    free_secret(key);
    return ret;
}

void mqtt_manager_client_cert_info(mqtt_client_cert_info_t *info)
{
    if (!info) return;
    memset(info, 0, sizeof(*info));
    struct stat st;
    info->has_key = stat(CFG_PATH_MQTT_KEY, &st) == 0;
    char *pem = load_pem(CFG_PATH_MQTT_CERT, MQTT_CLIENT_PEM_MAX);
    if (!pem) return;
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    if (mbedtls_x509_crt_parse(&crt, (const unsigned char *)pem, strlen(pem) + 1) >= 0 && crt.raw.p) {
        info->has_cert = true;
        if (mbedtls_x509_dn_gets(info->subject, sizeof(info->subject), &crt.subject) < 0)
            info->subject[0] = '\0';
        snprintf(info->not_after, sizeof(info->not_after), "%04d-%02d-%02d",
                 crt.valid_to.year, crt.valid_to.mon, crt.valid_to.day);
        char *key = info->has_key ? load_pem(CFG_PATH_MQTT_KEY, MQTT_CLIENT_PEM_MAX) : NULL;
        if (key) {
            mbedtls_pk_context pk;
            mbedtls_pk_init(&pk);
            info->key_match =
                mbedtls_pk_parse_key(&pk, (const unsigned char *)key, strlen(key) + 1, NULL, 0, rng_fill, NULL) == 0 &&
                mbedtls_pk_check_pair(&crt.pk, &pk, rng_fill, NULL) == 0;
            mbedtls_pk_free(&pk);
            free_secret(key);
        }
    }
    mbedtls_x509_crt_free(&crt);
    free(pem);
}

void mqtt_manager_last_error(char *buf, size_t len)
{
    if (!buf || !len) return;
    portENTER_CRITICAL(&s_err_mux);
    strlcpy(buf, s_last_error, len);
    portEXIT_CRITICAL(&s_err_mux);
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

    // Construire l'URI: "mqtt://host:port", "mqtts://host:port" en TLS
    const bool tls = gen->tls_mqtt != 0;
    const int def_port = tls ? 8883 : 1883;
    int port = def_port;
    if (gen->port_mqtt[0] != '\0') {
        port = atoi(gen->port_mqtt);
        if (port <= 0) port = def_port;
    }
    snprintf(s_uri, sizeof(s_uri), "%s://%s:%d", tls ? "mqtts" : "mqtt", gen->serv_mqtt, port);
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

    // TLS : le nom du serveur (servMQTT) est verifie contre le certificat
    // presente ; l'autorite est soit le bundle public d'ESP-IDF (Let's
    // Encrypt, Amazon Root CA, DigiCert...), soit le certificat importe
    if (gen->tls_mqtt == 2) {
        s_ca_pem = load_pem(CFG_PATH_MQTT_CA, MQTT_CA_PEM_MAX);
        if (!s_ca_pem) {
            ESP_LOGE(TAG, "TLS : certificat d'autorite absent ou illisible (%s)", CFG_PATH_MQTT_CA);
            set_last_error("Certificat de l'autorité privée absent");
            s_initialized = true;
            return ESP_OK;
        }
        mqtt_cfg.broker.verification.certificate = s_ca_pem;
    } else if (tls) {
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }

    // TLS mutuel : le broker authentifie la box par son certificat (AWS IoT
    // Core, RabbitMQ...) ; identifiant et mot de passe restent possibles
    if (tls && gen->cert_mqtt) {
        s_client_cert = load_pem(CFG_PATH_MQTT_CERT, MQTT_CLIENT_PEM_MAX);
        s_client_key = load_pem(CFG_PATH_MQTT_KEY, MQTT_CLIENT_PEM_MAX);
        if (!s_client_cert || !s_client_key) {
            ESP_LOGE(TAG, "TLS : certificat client ou cle privee absent");
            set_last_error("Certificat client ou clé privée absent");
            free_tls_buffers();
            s_initialized = true;
            return ESP_OK;
        }
        mqtt_cfg.credentials.authentication.certificate = s_client_cert;
        mqtt_cfg.credentials.authentication.key = s_client_key;
    }

    // ALPN : AWS IoT Core accepte le TLS mutuel sur le port 443 avec
    // "x-amzn-mqtt-ca" (8883 n'en demande pas)
    if (tls && gen->alpn_mqtt[0]) {
        strlcpy(s_alpn_buf, gen->alpn_mqtt, sizeof(s_alpn_buf));
        mqtt_cfg.broker.verification.alpn_protos = s_alpn_protos;
    }

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
        free_tls_buffers();
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

static bool dd_hex_num(uint64_t ieee, uint16_t cl, uint16_t at, int64_t *out)
{
    char buf[DEVDATA_VALUE_LEN];
    if (device_data_get_value(ieee, cl, at, buf, sizeof(buf)) != ESP_OK || !buf[0]) return false;
    *out = (int64_t)strtoull(buf, NULL, 16);
    return true;
}

/**
 * Puissance apparente d'un ZLinky producteur qui injecte : le Linky donne alors
 * SINSTS = 0 (0B04/050F, 090F, 0A0F, total 0306) alors que le courant passe.
 * Comme la LiXee-Box et la page Energie, la valeur publiee est -U x I, par
 * phase ou sommee pour le total, en mode standard et si une production est
 * attendue (Linky producteur, ou production configuree pour ce ZLinky).
 */
static bool injected_va(uint64_t ieee, uint16_t attr, int64_t *out)
{
    static const uint16_t PAPP[3] = { 0x050F, 0x090F, 0x0A0F };
    static const uint16_t VOLT[3] = { 0x0505, 0x0905, 0x0A05 };
    static const uint16_t CURR[3] = { 0x0508, 0x0908, 0x0A08 };
    const cfg_general_t *g = cfg_get_general();
    bool main_zl = strtoull(g->zlinky, NULL, 16) == ieee;
    int64_t mode;
    if (!dd_hex_num(ieee, 0xFF66, 0x0300, &mode)) {
        if (!main_zl) return false;
        mode = g->linky_mode;
    }
    bool producer = mode == 5 || mode == 7 || (main_zl && g->production[0]);
    if (!producer || mode == 0 || mode == 2) return false;   // historique : pas d'estimation

    int first = -1, last = -1;
    if (attr == 0x0306) {
        first = 0; last = 2;
    } else {
        for (int p = 0; p < 3; p++) if (PAPP[p] == attr) first = last = p;
    }
    if (first < 0) return false;
    int64_t sum = 0;
    for (int p = first; p <= last; p++) {
        int64_t va, i, u;
        if (attr == 0x0306 && dd_hex_num(ieee, 0x0B04, PAPP[p], &va) && va != 0) continue;   // phase qui soutire
        if (!dd_hex_num(ieee, 0x0B04, CURR[p], &i) || i <= 0) continue;
        if (!dd_hex_num(ieee, 0x0B04, VOLT[p], &u) || u <= 0) continue;
        sum += u * i;
    }
    if (sum <= 0) return false;
    *out = -sum;
    return true;
}

esp_err_t mqtt_manager_publish_attr(uint64_t ieee_addr, uint16_t cluster, uint16_t attr,
                                    const char *hex, uint8_t zcl_type, bool has_num, int64_t num)
{
    // SINSTS a 0 pendant l'injection : valeur calculee, negative (issue #35)
    if (has_num && num == 0 && cluster == 0x0B04 &&
        (attr == 0x050F || attr == 0x090F || attr == 0x0A0F || attr == 0x0306)) {
        int64_t inj;
        if (injected_va(ieee_addr, attr, &inj)) num = inj;
    }
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
            devdata_attr_t *attr = devdata_attr_at(cl, j);
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
    free_tls_buffers();
    set_last_error(NULL);
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

// ============================================================================
// Device Setup - Configuration d'un appareil Zigbee a partir de son template
// Voir device_setup.h
// ============================================================================

#include "device_setup.h"
#include "zigate_driver.h"
#include "zigate_devices.h"
#include "device_data.h"
#include "poll_engine.h"
#include "config_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "DEV_SETUP";

// Espacement des trames envoyees a la ZiGate pendant la configuration
#define SETUP_FRAME_GAP_MS        100
// Attente du modele de l'appareil (lu juste apres la decouverte)
#define SETUP_MODEL_WAIT_MS       10000
// Attente de la reponse 0x8009 (IEEE du coordinateur)
#define SETUP_COORD_WAIT_MS       3000
// Attente du mode TIC du ZLinky (0xFF66 / 0x0300)
#define SETUP_LINKY_MODE_WAIT_MS  5000
// Delai de securite apres l'arrivee sur le reseau (echange de cles Zigbee),
// comme la passerelle Arduino avant le bind
#define SETUP_JOIN_SAFETY_MS      1000

// Template utilise pour un ZLinky dont le DeviceId n'a pas de fichier :
// les versions <= v14 s'annoncent en 0x0053 (Meter Interface), et seul le
// fichier 257.json (0x0101, v15+) decrit le ZLinky.
#define ZLINKY_FALLBACK_TEMPLATE  "257.json"

typedef struct {
    uint64_t ieee;
    bool     force;
} setup_request_t;

static QueueHandle_t s_queue = NULL;
static device_setup_template_cb_t s_template_cb = NULL;
static device_setup_progress_cb_t s_progress_cb = NULL;

static void wait_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static bool is_zlinky(const zigate_device_t *dev)
{
    return dev && strncmp(dev->model, "ZLinky", 6) == 0;
}

// ----------------------------------------------------------------------------
// Etapes
// ----------------------------------------------------------------------------

// Attend que le modele soit connu (lu apres la decouverte). Relance une fois
// la lecture des attributs Basic s'il tarde.
static void wait_for_model(uint64_t ieee)
{
    bool relaunched = false;
    for (uint32_t t = 0; t < SETUP_MODEL_WAIT_MS; t += 250) {
        zigate_device_t *dev = zigate_devices_find_by_ieee(ieee);
        if (!dev || dev->model[0]) return;
        if (!relaunched && t >= SETUP_MODEL_WAIT_MS / 2) {
            zigate_read_basic_attributes(dev->short_addr, dev->endpoint);
            relaunched = true;
        }
        wait_ms(250);
    }
}

// Charge le template de l'appareil : <DeviceId>.json, modele exact ou
// "default", dans tpu/ (templates de l'utilisateur) ou tp/ (livres), voir
// device_data_load_template. Un appareil dont le fichier de DeviceId ne
// declare pas le modele (nouveau modele) est reconnu par son nom, quel que
// soit le fichier qui le declare.
static bool load_template(const zigate_device_t *dev)
{
    char file[64];
    snprintf(file, sizeof(file), "%u.json", (unsigned)dev->device_id);

    // Certains Tuya EF00 (TS0601...) renvoient un modele ZCL generique alors
    // que leurs datapoints different selon le fabricant exact (ManufacturerName,
    // 0x0004) : si le fichier du DeviceId declare une cle exacte sur ce nom
    // fabricant, elle prime sur le modele (pas de recherche inter-fichiers pour
    // elle, contrairement au modele ci-dessous).
    const char *key = dev->model;
    if (dev->manufacturer[0] && device_data_template_has_model(file, dev->manufacturer)) {
        key = dev->manufacturer;
    } else if (dev->model[0]) {
        char other[64];
        if (!device_data_template_has_model(file, dev->model) &&
            device_data_template_find_model(dev->model, file, other, sizeof(other))) {
            ESP_LOGI(TAG, "Modele '%s' (DeviceId 0x%04X) : template %s",
                     dev->model, dev->device_id, other);
            strlcpy(file, other, sizeof(file));
        }
    }

    esp_err_t err = device_data_load_template(dev->ieee_addr, file, key);
    if (err != ESP_OK && is_zlinky(dev) && strcmp(file, ZLINKY_FALLBACK_TEMPLATE) != 0) {
        ESP_LOGI(TAG, "ZLinky en DeviceId 0x%04X : template %s", dev->device_id,
                 ZLINKY_FALLBACK_TEMPLATE);
        err = device_data_load_template(dev->ieee_addr, ZLINKY_FALLBACK_TEMPLATE, dev->model);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "0x%016llX : pas de template pour DeviceId %u / modele '%s'",
                 (unsigned long long)dev->ieee_addr, (unsigned)dev->device_id, dev->model);
        return false;
    }
    // Template generique ("default" du DeviceId, pas de cle pour ce modele) :
    // seulement les clusters que l'appareil declare (#56). Appareil decouvert
    // avant la v3.4 : clusters inconnus, template garde tel quel.
    if (dev->in_cluster_count > 0 && !(key && key[0] && device_data_template_has_model(file, key))) {
        int n = device_data_template_keep_clusters(dev->ieee_addr, dev->in_clusters,
                                                   dev->in_cluster_count);
        if (n) ESP_LOGI(TAG, "0x%016llX : %d ligne(s) du template generique retiree(s)",
                        (unsigned long long)dev->ieee_addr, n);
    }
    return true;
}

// IEEE du coordinateur, indispensable au bind.
static uint64_t get_coordinator_ieee(void)
{
    uint64_t c = zigate_get_coordinator_ieee();
    if (c) return c;
    zigate_send_command(ZIGATE_CMD_NETWORK_STATE, NULL, 0);
    for (uint32_t t = 0; t < SETUP_COORD_WAIT_MS && !c; t += 100) {
        wait_ms(100);
        c = zigate_get_coordinator_ieee();
    }
    return c;
}

// Traitement specifique au ZLinky (SpecificTreatment de l'Arduino) :
// designer la source d'energie si aucune, et lire le mode TIC, qui filtre
// les entrees "report" du template. Renvoie le mode, ou -1 s'il est inconnu.
static int zlinky_specific(const zigate_device_t *dev)
{
    cfg_general_t *g = cfg_get_general_mut();
    // Comme la LiXee-Box : le ZLinky appaire devient la source d'energie
    // (compteur remplace, ZLinky change)
    char want[sizeof(g->zlinky)];
    snprintf(want, sizeof(want), "%016llX", (unsigned long long)dev->ieee_addr);
    if (strcasecmp(g->zlinky, want) != 0) {
        strlcpy(g->zlinky, want, sizeof(g->zlinky));
        cfg_save_general();
        ESP_LOGI(TAG, "ZLinky 0x%016llX designe comme source d'energie",
                 (unsigned long long)dev->ieee_addr);
    }

    device_data_t *dd = device_data_find(dev->ieee_addr);
    if (dd) dd->info.linky_mode[0] = '\0';   // attendre une valeur fraiche

    zigate_read_attribute(dev->short_addr, dev->endpoint, 0xFF66, 0x0300);   // LinkyMode
    wait_ms(SETUP_FRAME_GAP_MS);
    zigate_read_attribute(dev->short_addr, dev->endpoint, 0x0702, 0x0308);   // n° de serie
    // Lectures immediates de la LiXee-Box (SpecificTreatment) : tarif en cours
    // (standard et historique), option tarifaire, intensite souscrite
    static const uint16_t EXTRA[4][2] = { { 0xFF66, 0x0200 }, { 0x0702, 0x0020 },
                                          { 0xFF66, 0x0000 }, { 0x0B01, 0x000D } };
    for (int i = 0; i < 4; i++) {
        wait_ms(SETUP_FRAME_GAP_MS);
        zigate_read_attribute(dev->short_addr, dev->endpoint, EXTRA[i][0], EXTRA[i][1]);
    }

    for (uint32_t t = 0; t < SETUP_LINKY_MODE_WAIT_MS; t += 100) {
        dd = device_data_find(dev->ieee_addr);
        if (dd && dd->info.linky_mode[0]) {
            int mode = atoi(dd->info.linky_mode);
            if (g->linky_mode != mode) {
                g->linky_mode = mode;
                cfg_save_general();
            }
            ESP_LOGI(TAG, "ZLinky : mode TIC %d", mode);
            return mode;
        }
        wait_ms(100);
    }
    ESP_LOGW(TAG, "ZLinky : mode TIC non recu, reporting configure pour tous les modes");
    return -1;
}

/**
 * Endpoints ou configurer un cluster (bind, reporting) : l'endpoint impose
 * par le template s'il y en a un ; sinon celui qui porte le cluster
 * (l'endpoint principal en priorite) et ceux des lignes du template
 * declarees sur un autre endpoint ("endpoint": 3). Avant la v3.4, tout
 * partait vers l'endpoint de l'appareil : la sonde 0x0402 d'un endpoint 3 ou
 * 4 n'etait jamais configuree.
 */
static int cluster_targets(const zigate_device_t *dev, const devdata_template_t *tmpl,
                           uint16_t cluster, uint8_t forced_ep, uint8_t *out, int max)
{
    int n = 0;
    auto add = [&](uint8_t ep) {
        if (!ep) return;
        for (int i = 0; i < n; i++) if (out[i] == ep) return;
        if (n < max) out[n++] = ep;
    };
    if (forced_ep) {
        add(forced_ep);
        return n;
    }
    add(zigate_device_cluster_ep(dev, cluster));
    for (int i = 0; i < tmpl->state_count; i++) {
        const devdata_template_state_t *s = &tmpl->states[i];
        if (s->cluster == cluster && s->endpoint) add(s->endpoint);
    }
    return n;
}

#define SETUP_INITIAL_READS_MAX 24

/**
 * Lecture initiale des lignes du template, juste apres le bind et le
 * reporting : un appareil ne rapporte qu'au changement d'une valeur ou au
 * bout de l'intervalle maximal (5 min souvent), et sa fiche restait vide
 * jusque-la. Chaque attribut est lu sur son endpoint (ligne propre a un
 * endpoint, sinon celui qui porte le cluster). Pas pour le ZLinky (lectures
 * propres, dizaines d'attributs) ni pour les datapoints Tuya (EF00).
 */
static void send_initial_reads(const zigate_device_t *dev, const devdata_template_t *tmpl)
{
    int sent = 0;
    for (int i = 0; i < tmpl->state_count && sent < SETUP_INITIAL_READS_MAX; i++) {
        const devdata_template_state_t *s = &tmpl->states[i];
        if (s->cluster == 0xEF00) continue;
        // Cluster absent de l'appareil (quand ses clusters sont connus)
        if (dev->in_cluster_count && !zigate_device_has_cluster(dev, s->cluster)) continue;
        // Meme attribut deja lu (deux lignes sur une meme valeur)
        bool dup = false;
        for (int k = 0; k < i && !dup; k++) {
            dup = tmpl->states[k].cluster == s->cluster && tmpl->states[k].attribute == s->attribute;
        }
        if (dup) continue;
        uint8_t ep = s->endpoint ? s->endpoint : zigate_device_cluster_ep(dev, s->cluster);
        uint16_t attr = s->endpoint ? (uint16_t)(s->attribute & 0x0FFF) : s->attribute;
        if (s->manufacturer_code) {
            zigate_read_attribute_mfr(dev->short_addr, ep, s->cluster, attr, s->manufacturer_code);
        } else {
            zigate_read_attribute(dev->short_addr, ep, s->cluster, attr);
        }
        sent++;
        wait_ms(SETUP_FRAME_GAP_MS);
    }
    if (sent) {
        ESP_LOGI(TAG, "Lecture initiale 0x%016llX : %d attribut(s)",
                 (unsigned long long)dev->ieee_addr, sent);
    }
}

static void send_binds(const zigate_device_t *dev, const devdata_template_t *tmpl)
{
    if (tmpl->bind_count == 0) return;

    uint64_t coord = get_coordinator_ieee();
    if (!coord) {
        ESP_LOGE(TAG, "IEEE du coordinateur inconnue : bind impossible");
        return;
    }
    for (int i = 0; i < tmpl->bind_count; i++) {
        uint8_t eps[ZIGATE_MAX_EPS];
        int n = cluster_targets(dev, tmpl, tmpl->bind_clusters[i], 0, eps, ZIGATE_MAX_EPS);
        for (int k = 0; k < n; k++) {
            zigate_bind(dev->ieee_addr, eps[k], tmpl->bind_clusters[i], coord, 0x01);
            ESP_LOGI(TAG, "Bind 0x%016llX ep %u cluster 0x%04X -> coordinateur",
                     (unsigned long long)dev->ieee_addr, eps[k], tmpl->bind_clusters[i]);
            wait_ms(SETUP_FRAME_GAP_MS);
        }
    }
}

static void send_reports(const zigate_device_t *dev, const devdata_template_t *tmpl, int linky_mode)
{
    int sent = 0, filtered = 0;
    for (int i = 0; i < tmpl->report_count; i++) {
        const devdata_template_report_t *r = &tmpl->reports[i];

        // Filtre par mode Linky : une entree avec "mode" ne vaut que pour les
        // modes listes. Mode inconnu : on configure tout.
        if (r->mode_mask && linky_mode >= 0 && linky_mode <= 7 &&
            !(r->mode_mask & (1u << linky_mode))) {
            filtered++;
            continue;
        }

        uint8_t eps[ZIGATE_MAX_EPS];
        int n = cluster_targets(dev, tmpl, r->cluster, r->endpoint, eps, ZIGATE_MAX_EPS);
        for (int k = 0; k < n; k++) {
            zigate_configure_reporting(dev->short_addr, eps[k], r->cluster, r->attribute,
                                       r->type, r->min_interval, r->max_interval, r->timeout,
                                       r->change, r->manufacturer_code);
            sent++;
            wait_ms(SETUP_FRAME_GAP_MS);
        }
    }
    ESP_LOGI(TAG, "Config report 0x%016llX : %d envoye(s), %d hors mode",
             (unsigned long long)dev->ieee_addr, sent, filtered);
}

// ----------------------------------------------------------------------------
// Sequence complete pour un appareil
// ----------------------------------------------------------------------------

static void setup_device(const setup_request_t *req)
{
    if (req->force) {
        wait_ms(SETUP_JOIN_SAFETY_MS);
    }
    wait_for_model(req->ieee);

    zigate_device_t *dev = zigate_devices_find_by_ieee(req->ieee);
    if (!dev) return;

    // Copie : la base peut etre modifiee par d'autres taches pendant la sequence
    zigate_device_t snap = *dev;

    if (!device_data_get_or_create(snap.ieee_addr)) {
        ESP_LOGE(TAG, "0x%016llX : device_data indisponible", (unsigned long long)snap.ieee_addr);
        if (s_template_cb) s_template_cb(snap.ieee_addr, false);
        return;
    }
    bool tmpl_ok = load_template(&snap);
    // Template connu (ou absent) : l'integrateur peut exposer l'appareil
    // (Matter), avant le bind et le reporting qui prennent plusieurs secondes
    if (s_template_cb) s_template_cb(snap.ieee_addr, tmpl_ok);
    if (!tmpl_ok) return;

    device_data_t *dd = device_data_find(snap.ieee_addr);
    const devdata_template_t *tmpl = dd ? dd->tmpl : NULL;
    if (!tmpl || !tmpl->loaded) return;

    bool configure = req->force || !snap.configured;
    if (configure) {
        ESP_LOGI(TAG, "Configuration de 0x%016llX (modele '%s', %d bind, %d report)",
                 (unsigned long long)snap.ieee_addr, snap.model,
                 tmpl->bind_count, tmpl->report_count);

        int linky_mode = is_zlinky(&snap) ? zlinky_specific(&snap) : -1;
        if (s_progress_cb) s_progress_cb(snap.ieee_addr, "Bind en cours...");
        send_binds(&snap, tmpl);
        if (s_progress_cb) s_progress_cb(snap.ieee_addr, "Configuration des rapports...");
        send_reports(&snap, tmpl, linky_mode);
        if (!is_zlinky(&snap)) send_initial_reads(&snap, tmpl);
        zigate_devices_set_configured(snap.ieee_addr, true);
        if (s_progress_cb) s_progress_cb(snap.ieee_addr, "Configuration OK");
    }

    // Version logicielle, lue a l'appairage comme la LiXee-Box. Au demarrage
    // du pont, seulement si elle est inconnue et l'appareil sur secteur : un
    // appareil sur pile dort et la lecture echouait a chaque redemarrage
    // (alertes F0 / D4 sans raison)
    {
        const char *ver = device_data_get_value_ptr(snap.ieee_addr, 0x0000, 0x4000);
        const char *bat = device_data_get_value_ptr(snap.ieee_addr, 0x0001, 0x0021);
        const char *volt = device_data_get_value_ptr(snap.ieee_addr, 0x0001, 0x0020);
        bool on_battery = (bat && bat[0]) || (volt && volt[0]);
        if (configure || (!(ver && ver[0]) && !on_battery)) {
            zigate_read_attribute(snap.short_addr, snap.endpoint ? snap.endpoint : 1, 0x0000, 0x4000);
        }
        // Source d'alimentation (pile ou secteur), lue a l'appairage : decide
        // si les echecs des requetes reseau de la ZiGate sont signales
        if (configure) {
            vTaskDelay(pdMS_TO_TICKS(150));
            zigate_read_attribute(snap.short_addr, snap.endpoint ? snap.endpoint : 1, 0x0000, 0x0007);
        }
    }

    // Liste de poll : reconstruite depuis le template a la reconfiguration
    // (zigbee.cpp de la Box), sinon gardee (elle peut avoir ete personnalisee)
    if (req->force) {
        device_data_t *pd = device_data_find(snap.ieee_addr);
        if (pd) pd->poll_count = 0;
    }
    poll_engine_load_device_poll_list(snap.ieee_addr);
    device_data_save(snap.ieee_addr);
}

static void setup_task(void *arg)
{
    setup_request_t req;
    for (;;) {
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) == pdTRUE) {
            setup_device(&req);
        }
    }
}

// ----------------------------------------------------------------------------
// API
// ----------------------------------------------------------------------------

esp_err_t device_setup_init(void)
{
    if (s_queue) return ESP_OK;
    s_queue = xQueueCreate(ZIGATE_MAX_DEVICES, sizeof(setup_request_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(setup_task, "dev_setup", 6144, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void device_setup_request(uint64_t ieee_addr, bool force)
{
    if (!s_queue) return;
    setup_request_t req = { ieee_addr, force };
    if (xQueueSend(s_queue, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "File de configuration pleine, 0x%016llX ignore",
                 (unsigned long long)ieee_addr);
    }
}

void device_setup_register_template_cb(device_setup_template_cb_t cb) { s_template_cb = cb; }
void device_setup_register_progress_cb(device_setup_progress_cb_t cb) { s_progress_cb = cb; }

bool device_setup_load_template_now(uint64_t ieee_addr)
{
    zigate_device_t *dev = zigate_devices_find_by_ieee(ieee_addr);
    if (!dev) return false;
    zigate_device_t snap = *dev;
    if (!device_data_get_or_create(snap.ieee_addr)) return false;
    return load_template(&snap);
}

void device_setup_boot(void)
{
    int n = zigate_devices_count();
    for (int i = 0; i < n; i++) {
        zigate_device_t *dev = zigate_devices_get(i);
        if (dev && dev->active) {
            device_setup_request(dev->ieee_addr, false);
        }
    }
    ESP_LOGI(TAG, "%d appareil(s) en file de configuration", n);
}

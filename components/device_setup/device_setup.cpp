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

// Charge le template de l'appareil : /littlefs/tp/<DeviceId>.json, modele
// exact ou "default".
static bool load_template(const zigate_device_t *dev)
{
    char file[16];
    snprintf(file, sizeof(file), "%u.json", (unsigned)dev->device_id);

    esp_err_t err = device_data_load_template(dev->ieee_addr, file, dev->model);
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

static void send_binds(const zigate_device_t *dev, const devdata_template_t *tmpl)
{
    if (tmpl->bind_count == 0) return;

    uint64_t coord = get_coordinator_ieee();
    if (!coord) {
        ESP_LOGE(TAG, "IEEE du coordinateur inconnue : bind impossible");
        return;
    }
    for (int i = 0; i < tmpl->bind_count; i++) {
        zigate_bind(dev->ieee_addr, dev->endpoint, tmpl->bind_clusters[i], coord, 0x01);
        ESP_LOGI(TAG, "Bind 0x%016llX cluster 0x%04X -> coordinateur",
                 (unsigned long long)dev->ieee_addr, tmpl->bind_clusters[i]);
        wait_ms(SETUP_FRAME_GAP_MS);
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

        zigate_configure_reporting(dev->short_addr, dev->endpoint, r->cluster, r->attribute,
                                   r->type, r->min_interval, r->max_interval, r->timeout,
                                   r->change, r->manufacturer_code);
        sent++;
        wait_ms(SETUP_FRAME_GAP_MS);
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

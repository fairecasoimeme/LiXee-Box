// ============================================================================
// Poll Engine - Interrogation periodique des attributs Zigbee
//
// Port de ScanDeviceToPoll() + scanCallback() Arduino vers ESP-IDF.
// Tache FreeRTOS periodique qui parcourt les poll lists de chaque device
// et envoie des zigate_read_attribute quand l'intervalle est ecoule.
// ============================================================================

#include "poll_engine.h"

#include <string.h>
#include <stdio.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "device_data.h"
#include "zigate_driver.h"
#include "zigate_devices.h"

static const char *TAG = "POLL_ENG";

// ============================================================================
// Etat interne
// ============================================================================

static TaskHandle_t s_poll_task = NULL;
static bool s_running = false;
static bool s_trigger_now = false;
static uint32_t s_interval_ms = POLL_ENGINE_DEFAULT_INTERVAL_MS;
static bool s_initialized = false;

// ============================================================================
// Helpers
// ============================================================================

/**
 * Retourne le timestamp courant en millisecondes (monotonic).
 * Utilise esp_timer_get_time() (microsecondes) / 1000.
 */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/**
 * Scan un device: parcourt sa poll list et envoie les read_attribute
 * dont l'intervalle est depasse.
 * @return Nombre de requetes envoyees pour ce device
 */
static int scan_device(device_data_t *dd)
{
    if (!dd || dd->poll_count == 0) return 0;

    // Trouver le device zigbee correspondant (pour short_addr + endpoint)
    zigate_device_t *zdev = zigate_devices_find_by_ieee(dd->ieee_addr);
    if (!zdev || !zdev->active) {
        return 0;
    }

    uint32_t current_time = now_ms();
    int sent = 0;

    for (int i = 0; i < dd->poll_count; i++) {
        devdata_poll_item_t *pi = &dd->poll_list[i];

        // Verifier si l'intervalle est ecoule
        // Cas special: last_poll == 0 → premier poll, envoyer immediatement
        if (pi->last_poll != 0) {
            uint32_t elapsed = current_time - pi->last_poll;
            if (elapsed < pi->interval_ms) {
                continue;
            }
        }

        // Envoyer la requete de lecture
        ESP_LOGD(TAG, "Poll 0x%016llX: cluster=0x%04X attr=0x%04X mfr=0x%04X",
                 (unsigned long long)dd->ieee_addr,
                 pi->cluster_id, pi->attr_id, pi->manufacturer_code);

        // Ligne propre a un endpoint : attribut reel lu sur cet endpoint
        uint8_t kep = 0;
        uint16_t attr = device_data_resolve_key(dd->ieee_addr, pi->cluster_id, pi->attr_id, &kep);
        uint8_t ep = kep ? kep : zdev->endpoint;
        esp_err_t ret;
        if (pi->manufacturer_code != 0) {
            ret = zigate_read_attribute_mfr(zdev->short_addr, ep,
                                             pi->cluster_id, attr,
                                             pi->manufacturer_code);
        } else {
            ret = zigate_read_attribute(zdev->short_addr, ep,
                                         pi->cluster_id, attr);
        }

        if (ret == ESP_OK) {
            // Mettre a jour le timestamp de dernier poll
            pi->last_poll = current_time;
            sent++;

            // Delai entre les requetes pour ne pas saturer le bus Zigbee
            if (POLL_ENGINE_INTER_REQUEST_DELAY_MS > 0) {
                vTaskDelay(pdMS_TO_TICKS(POLL_ENGINE_INTER_REQUEST_DELAY_MS));
            }
        } else {
            ESP_LOGW(TAG, "Failed to send read_attribute: %s", esp_err_to_name(ret));
        }
    }

    return sent;
}

/**
 * Parcourt tous les devices et execute leur scan.
 * Equivalent de ScanDeviceToPoll() Arduino.
 */
static void scan_all_devices(void)
{
    int total_sent = 0;
    int device_count = device_data_count();

    for (int i = 0; i < device_count; i++) {
        device_data_t *dd = device_data_get_by_index(i);
        if (!dd) continue;

        int sent = scan_device(dd);
        total_sent += sent;
    }

    if (total_sent > 0) {
        ESP_LOGI(TAG, "Poll cycle: sent %d read requests across %d devices",
                 total_sent, device_count);
    }
}

/**
 * Charge la poll list d'un device depuis son template.
 * Les items "poll" du template sont convertis en poll_items dans device_data.
 *
 * Format Arduino du template poll:
 *   "poll": [
 *     { "cluster": "0x0702", "attribut": 0, "poll": 60000, "manufacturerCode": 0 },
 *     ...
 *   ]
 *
 * Le "poll" est deja en ms dans la version ESP-IDF (converti depuis cycles*60000 Arduino).
 * Mais dans les templates originaux, "poll" peut etre en nombre de cycles (1 = 60s).
 * On detecte: si poll < 1000, c'est en cycles (on multiplie par 60000).
 */
static esp_err_t load_poll_list_from_template(device_data_t *dd)
{
    if (!dd || !dd->tmpl || !dd->tmpl->loaded) {
        return ESP_ERR_NOT_FOUND;
    }

    // Si le device a deja une poll list chargee (depuis le JSON),
    // ne pas ecraser (elle peut avoir ete personnalisee)
    if (dd->poll_count > 0) {
        ESP_LOGD(TAG, "Device 0x%016llX already has %d poll items, skipping template load",
                 (unsigned long long)dd->ieee_addr, dd->poll_count);
        return ESP_OK;
    }

    // Seuls les etats marques "poll" dans le template sont scrutes, a la
    // periode indiquee (en minutes, comme le cycle de 60 s de la passerelle
    // Arduino). L'ancienne version scrutait les 16 premiers etats quels qu'ils
    // soient : pour le ZLinky (59 etats), des attributs absents du mode du
    // compteur, et jamais ceux qui comptent.
    // Les mesures (index, puissance, tension, courant) ne sont PAS scrutees :
    // elles arrivent par le reporting configure depuis la section "report".
    const devdata_template_t *tmpl = dd->tmpl;
    int added = 0, skipped = 0;
    for (int i = 0; i < tmpl->state_count; i++) {
        const devdata_template_state_t *s = &tmpl->states[i];
        if (s->poll == 0) continue;

        uint32_t interval = (uint32_t)s->poll * 60000u;
        esp_err_t err = device_data_add_poll_item(dd->ieee_addr, s->cluster, s->attribute,
                                                  interval, s->manufacturer_code);
        if (err == ESP_OK) {
            added++;
        } else {
            skipped++;
        }
    }

    if (skipped > 0) {
        ESP_LOGW(TAG, "Device 0x%016llX : %d element(s) de poll non ajoutes (liste pleine ?)",
                 (unsigned long long)dd->ieee_addr, skipped);
    }
    ESP_LOGI(TAG, "Added %d poll items from template for device 0x%016llX",
             added, (unsigned long long)dd->ieee_addr);
    return ESP_OK;
}

// ============================================================================
// Tache FreeRTOS
// ============================================================================

static void poll_engine_task(void *arg)
{
    ESP_LOGI(TAG, "Poll engine task started (interval=%lu ms)", (unsigned long)s_interval_ms);

    // Delai initial pour laisser le reseau Zigbee se stabiliser
    vTaskDelay(pdMS_TO_TICKS(10000));

    // Charger les poll lists depuis les templates au premier demarrage
    poll_engine_reload_poll_lists();

    while (s_running) {
        // Attendre l'intervalle ou un trigger manuel
        // On decoupe l'attente en blocs de 1 seconde pour etre reactif
        uint32_t waited = 0;
        while (waited < s_interval_ms && s_running && !s_trigger_now) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            waited += 1000;
        }

        if (!s_running) break;

        // Reset le trigger
        s_trigger_now = false;

        // Cycle de poll
        ESP_LOGD(TAG, "--- Poll cycle start ---");
        scan_all_devices();
        ESP_LOGD(TAG, "--- Poll cycle end ---");
    }

    ESP_LOGI(TAG, "Poll engine task stopped");
    s_poll_task = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// API publique
// ============================================================================

esp_err_t poll_engine_init(void)
{
    if (s_initialized) return ESP_OK;

    s_running = false;
    s_trigger_now = false;
    s_poll_task = NULL;
    s_interval_ms = POLL_ENGINE_DEFAULT_INTERVAL_MS;

    s_initialized = true;
    ESP_LOGI(TAG, "Poll engine initialized");
    return ESP_OK;
}

esp_err_t poll_engine_start(uint32_t interval_ms)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Poll engine not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_running) {
        ESP_LOGW(TAG, "Poll engine already running");
        return ESP_OK;
    }

    s_interval_ms = (interval_ms > 0) ? interval_ms : POLL_ENGINE_DEFAULT_INTERVAL_MS;
    s_running = true;

    BaseType_t ret = xTaskCreate(
        poll_engine_task,
        "poll_engine",
        POLL_ENGINE_TASK_STACK_SIZE,
        NULL,
        POLL_ENGINE_TASK_PRIORITY,
        &s_poll_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create poll engine task");
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Poll engine started (interval=%lu ms)", (unsigned long)s_interval_ms);
    return ESP_OK;
}

esp_err_t poll_engine_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    s_running = false;
    // La tache se terminera a la prochaine iteration (max 1s de delai)
    ESP_LOGI(TAG, "Poll engine stop requested");
    return ESP_OK;
}

bool poll_engine_is_running(void)
{
    return s_running;
}

esp_err_t poll_engine_trigger_now(void)
{
    if (!s_running) {
        ESP_LOGW(TAG, "Poll engine not running, cannot trigger");
        return ESP_ERR_INVALID_STATE;
    }

    s_trigger_now = true;
    ESP_LOGI(TAG, "Manual poll trigger requested");
    return ESP_OK;
}

esp_err_t poll_engine_reload_poll_lists(void)
{
    int count = device_data_count();
    int loaded = 0;

    for (int i = 0; i < count; i++) {
        device_data_t *dd = device_data_get_by_index(i);
        if (!dd) continue;

        if (load_poll_list_from_template(dd) == ESP_OK) {
            loaded++;
        }
    }

    ESP_LOGI(TAG, "Reloaded poll lists: %d/%d devices processed", loaded, count);
    return ESP_OK;
}

esp_err_t poll_engine_load_device_poll_list(uint64_t ieee_addr)
{
    device_data_t *dd = device_data_find(ieee_addr);
    if (!dd) {
        ESP_LOGW(TAG, "Device 0x%016llX not found for poll list load",
                 (unsigned long long)ieee_addr);
        return ESP_ERR_NOT_FOUND;
    }

    return load_poll_list_from_template(dd);
}

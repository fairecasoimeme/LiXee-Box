#pragma once

// ============================================================================
// Poll Engine - Interrogation periodique des attributs Zigbee
//
// Equivalent de ScanDeviceToPoll() + scanCallback() Arduino.
//
// Architecture:
//   - Tache FreeRTOS qui s'execute periodiquement (configurable, defaut 60s)
//   - Pour chaque device, parcourt sa poll_list (dans device_data)
//   - Si l'intervalle est ecoule, envoie zigate_read_attribute_mfr()
//   - Les reponses reviennent via le callback attribut deja en place
//   - Gere aussi la sauvegarde periodique et le status des devices
//
// Le poll_list de chaque device est charge depuis le template JSON
// (poll array dans /littlefs/db/<IEEE>.json) ou ajoute dynamiquement.
//
// Cycle de base Arduino: 60 secondes
//   ScanDeviceToPoll: itere devices, decremente compteurs, envoie read
//   scanCallback: save all, update time, check rules, reset counters
// ============================================================================

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration
// ============================================================================

/** Intervalle de base du cycle de poll en millisecondes (defaut: 60s comme Arduino) */
#define POLL_ENGINE_DEFAULT_INTERVAL_MS    60000

/** Delai entre 2 envois de read_attribute pour ne pas saturer la ZiGate (ms) */
#define POLL_ENGINE_INTER_REQUEST_DELAY_MS 100

/** Priorite de la tache FreeRTOS (faible: pas critique) */
#define POLL_ENGINE_TASK_PRIORITY          3

/** Taille de la stack de la tache (suffisant pour cJSON + logs) */
#define POLL_ENGINE_TASK_STACK_SIZE     6144    // chaque lecture ZiGate empile paquet + trame encodee + console

// ============================================================================
// API publique
// ============================================================================

/**
 * Initialise le poll engine.
 * N'alloue aucune ressource tant que poll_engine_start() n'est pas appele.
 */
esp_err_t poll_engine_init(void);

/**
 * Demarre la tache de polling periodique.
 * Cree une tache FreeRTOS qui interroge les devices a intervalles reguliers.
 * @param interval_ms  Intervalle de cycle en ms (0 = utiliser le defaut 60000)
 */
esp_err_t poll_engine_start(uint32_t interval_ms);

/**
 * Arrete la tache de polling.
 * La tache sera supprimee a la prochaine iteration.
 */
esp_err_t poll_engine_stop(void);

/**
 * Retourne true si le poll engine est en cours d'execution.
 */
bool poll_engine_is_running(void);

/**
 * Force un cycle de poll immediat (hors du timer).
 * Utile pour rafraichir apres un appairage.
 */
esp_err_t poll_engine_trigger_now(void);

/**
 * Charge les poll lists depuis les templates pour tous les devices connus.
 * Appele automatiquement au demarrage et apres decouverte d'un device.
 * Peut aussi etre appele manuellement si les templates changent.
 */
esp_err_t poll_engine_reload_poll_lists(void);

/**
 * Charge la poll list depuis le template pour un device specifique.
 * Appele apres la decouverte d'un nouveau device.
 * @param ieee_addr  Adresse IEEE du device
 */
esp_err_t poll_engine_load_device_poll_list(uint64_t ieee_addr);

#ifdef __cplusplus
}
#endif

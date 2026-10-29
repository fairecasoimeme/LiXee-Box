#pragma once

// ============================================================================
// Cluster Handlers - Traitement des rapports d'attributs Zigbee par cluster
//
// Equivalent de readZigbeeDatas() + cluster handlers Arduino.
//
// Architecture:
//   - cluster_handler_dispatch() recoit un rapport brut (short_addr, cluster,
//     attribut, data bytes) et route vers le handler du cluster
//   - Chaque handler interprete les bytes, applique les coefficients,
//     et stocke la valeur interpretee dans device_data
//   - Les valeurs brutes sont deja stockees dans device_data par main.cpp
//     AVANT l'appel a cluster_handler_dispatch()
//   - Les handlers enrichissent avec des valeurs interpretees (ex: temperature
//     en degres, puissance en watts) utilisees par l'interface web
//
// Clusters geres:
//   0x0000 - Basic (manufacturer, model, SW version)
//   0x0001 - Power Configuration (batterie)
//   0x0006 - On/Off
//   0x0008 - Level Control
//   0x0101 - Door Lock
//   0x0102 - Window Covering
//   0x0201 - Thermostat
//   0x0402 - Temperature Measurement
//   0x0405 - Relative Humidity
//   0x0406 - Occupancy Sensing
//   0x0702 - Simple Metering (energie)
//   0x0B04 - Electrical Measurement (puissance, tension, courant)
//   0xFF66 - LiXee Custom (ZLinky)
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Structure du rapport d'attribut (identique a zigate_attribute_report_t
// mais sans inclure zigate_driver.h pour eviter les dependances circulaires)
// ============================================================================

typedef struct {
    uint64_t ieee_addr;        // Adresse IEEE du device source
    uint16_t short_addr;       // Adresse courte
    uint8_t  endpoint;         // Endpoint source
    uint16_t cluster_id;       // Cluster ZCL
    uint16_t attr_id;          // Attribut ZCL
    uint8_t  attr_type;        // Type ZCL de la donnee
    uint8_t  data[256];        // Bytes bruts
    uint16_t data_len;         // Longueur des bytes
} cluster_report_t;

// ============================================================================
// Valeur interpretee (resultat du handler)
// ============================================================================

typedef struct {
    float    float_value;      // Valeur numerique interpretee
    char     str_value[64];    // Valeur texte interpretee
    char     unit[16];         // Unite ("°C", "W", "V", "A", "%", "Wh", ...)
    bool     has_float;        // float_value est valide
    bool     has_string;       // str_value est valide
    bool     notify;           // Generer une notification pour cette valeur
    char     notify_msg[128];  // Message de notification
} cluster_interpreted_t;

// ============================================================================
// API publique
// ============================================================================

/**
 * Initialise le module cluster handlers.
 */
esp_err_t cluster_handlers_init(void);

/**
 * Dispatche un rapport d'attribut vers le handler du cluster.
 * Appele depuis main.cpp apres le stockage brut dans device_data.
 *
 * @param report  Rapport complet (ieee, cluster, attribut, data)
 * @param out     Valeur interpretee en sortie (peut etre NULL si pas besoin)
 * @return ESP_OK si traite, ESP_ERR_NOT_FOUND si cluster inconnu
 */
esp_err_t cluster_handler_dispatch(const cluster_report_t *report,
                                    cluster_interpreted_t *out);

// ============================================================================
// ZCL Data Types
// ============================================================================

#define ZCL_BOOL       0x10
#define ZCL_UINT8      0x20
#define ZCL_UINT16     0x21
#define ZCL_UINT24     0x22
#define ZCL_UINT32     0x23
#define ZCL_UINT48     0x25
#define ZCL_INT8       0x28
#define ZCL_INT16      0x29
#define ZCL_INT32      0x2B
#define ZCL_ENUM8      0x30
#define ZCL_ENUM16     0x31
#define ZCL_FLOAT16    0x38
#define ZCL_FLOAT32    0x39
#define ZCL_STRING     0x42
#define ZCL_LONG_STRING 0x43

// ============================================================================
// Helpers de conversion (utilises par les handlers)
// ============================================================================

/** Convertit des bytes big-endian en uint16 */
static inline uint16_t zcl_uint16(const uint8_t *data) {
    return (uint16_t)((data[0] << 8) | data[1]);
}

/** Convertit des bytes big-endian en int16 (signe) */
static inline int16_t zcl_int16(const uint8_t *data) {
    return (int16_t)((data[0] << 8) | data[1]);
}

/** Convertit des bytes big-endian en uint32 */
static inline uint32_t zcl_uint32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8)  | data[3];
}

/** Convertit des bytes big-endian en uint24 */
static inline uint32_t zcl_uint24(const uint8_t *data) {
    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
}

/** Convertit hex string en long (comme Arduino strtol sur hex) */
static inline long hex_str_to_long(const char *hex) {
    return strtol(hex, NULL, 16);
}

/** Largeur en octets d'un type ZCL numerique, 0 si inconnu ou non numerique */
static inline int zcl_type_width(uint8_t zcl_type) {
    // data8..64 (0x08-0x0F), bitmap8..64 (0x18-0x1F), uint8..64 (0x20-0x27),
    // int8..64 (0x28-0x2F) : largeur = rang dans la famille + 1
    if (zcl_type >= 0x08 && zcl_type <= 0x0F) return zcl_type - 0x07;
    if (zcl_type >= 0x18 && zcl_type <= 0x1F) return zcl_type - 0x17;
    if (zcl_type >= 0x20 && zcl_type <= 0x27) return zcl_type - 0x1F;
    if (zcl_type >= 0x28 && zcl_type <= 0x2F) return zcl_type - 0x27;
    switch (zcl_type) {
    case ZCL_BOOL: case ZCL_ENUM8:  return 1;
    case ZCL_ENUM16:                return 2;
    default:                        return 0;
    }
}

/** true si le type ZCL est un entier signe (int8 a int64) */
static inline bool zcl_type_is_signed(uint8_t zcl_type) {
    return zcl_type >= 0x28 && zcl_type <= 0x2F;
}

/**
 * Convertit des bytes ZCL big-endian en entier 64 bits EXACT, en respectant
 * le signe et la largeur reels du type.
 *
 * A utiliser partout ou une valeur numerique est derivee d'un rapport
 * d'attribut. Contrairement a interpret_numeric(), qui rend un float dont la
 * mantisse de 24 bits arrondit un index d'energie uint48, cette conversion est
 * exacte sur toute la plage des types ZCL manipules.
 *
 * @return false si les donnees sont trop courtes pour le type annonce
 */
static inline bool zcl_to_int64(const uint8_t *data, int len,
                                uint8_t zcl_type, int64_t *out) {
    if (!data || !out || len <= 0) return false;

    int w = zcl_type_width(zcl_type);
    if (w == 0) {
        // Type inconnu : se rabattre sur la longueur recue, lue comme non signee
        w = (len > 8) ? 8 : len;
    }
    if (len < w) return false;

    uint64_t v = 0;
    for (int i = 0; i < w; i++) {
        v = (v << 8) | data[i];
    }

    // Extension de signe sur la largeur du type (jamais 8 octets ici, donc
    // le decalage reste defini)
    if (zcl_type_is_signed(zcl_type) && w < 8) {
        uint64_t sign_bit = (uint64_t)1 << (w * 8 - 1);
        if (v & sign_bit) {
            v |= ~(((uint64_t)1 << (w * 8)) - 1);
        }
    }

    *out = (int64_t)v;
    return true;
}

#ifdef __cplusplus
}
#endif

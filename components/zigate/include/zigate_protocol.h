#pragma once

#include "zigate_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ZiGate Protocol - Encodage/Decodage du protocole serie ZiGate
// Le protocole utilise un framing avec byte-stuffing (XOR 0x02)
// Format: [0x01] [Type MSB] [Type LSB] [Len MSB] [Len LSB] [Checksum] [Payload...] [LQI] [0x03]
// ============================================================================

/**
 * Encode un paquet pour envoi via UART (byte-stuffing)
 * @param pkt Paquet source (pkt->length doit etre <= sizeof(pkt->payload))
 * @param out_buf Buffer de sortie
 * @param out_cap Capacite de out_buf, doit etre >= 2 * pkt->length + 12
 *                (pire cas: chaque octet < 0x10 est encode sur 2 octets)
 * @param out_len Taille resultante
 * @return ESP_OK en cas de succes, ESP_ERR_INVALID_SIZE si la capacite est
 *         insuffisante ou si pkt->length depasse la taille du payload
 */
esp_err_t zigate_encode_packet(const zigate_packet_t *pkt, uint8_t *out_buf,
                               size_t out_cap, size_t *out_len);

/**
 * Decode un flux de bytes bruts en paquet
 * Gere le byte-stuffing et la verification de checksum
 * @param raw Buffer brut recu (entre 0x01 et 0x03)
 * @param raw_len Taille du buffer
 * @param pkt Paquet decode en sortie
 * @return ESP_OK si decode valide, ESP_ERR_INVALID_CRC si checksum invalide
 */
esp_err_t zigate_decode_packet(const uint8_t *raw, size_t raw_len, zigate_packet_t *pkt);

/**
 * Calcule le checksum XOR d'un paquet
 * Checksum = XOR(Type MSB, Type LSB, Len MSB, Len LSB, Payload[0..n])
 */
uint8_t zigate_compute_checksum(uint16_t type, uint16_t length, const uint8_t *payload);

/**
 * Dispatche un paquet decode vers les handlers appropriate
 * Appele par la tache de reception apres decodage
 */
void zigate_dispatch_packet(const zigate_packet_t *pkt);

#ifdef __cplusplus
}
#endif

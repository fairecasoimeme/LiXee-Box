# ZLinky OTA — guide d'utilisation

> **Document historique.** Il décrit l'outil PC `ota_send.py`, qui pilotait la mise à jour par le
> port série du récepteur de référence, sans fenêtre (un ACK par bloc). La LiXee-Box (≥ v2.26)
> fait désormais la mise à jour elle-même, depuis **Réseau → LoRa**, avec un acquittement par
> fenêtre de 16 blocs : voir `LORA_PROTOCOL.md` §10, qui fait référence. Les durées annoncées
> plus bas (250 ms par bloc, 13 minutes) étaient une estimation ; un bloc dure ~505 ms à SF11.

Mise à jour firmware over-the-air via LoRa du ZLinky depuis ta Lixée-box.

## Architecture

```
PC (firmware.bin)
    │ Serial USB
    ▼
Receiver Arduino (gateway, "OTA gateway mode")
    │ LoRa SF11 BW406 sur op_channel
    ▼
ZLinky (en mode "OTA recovery", attend OTA_START)
```

## Procédure pour faire un OTA

### Étape 1 : préparer le ZLinky

1. **Débrancher** l'alim du ZLinky
2. **Maintenir appuyé** le bouton USERINTERFACE (PIO0)
3. **Rebrancher** l'alim, **continuer à maintenir** le bouton ~3 secondes
4. Au boot, le firmware détecte le bouton tenu → entre en mode OTA recovery
5. Logs UART montrent :
   ```
   LORA_OTA: button held at boot → entering OTA mode
   LORA_OTA: *** OTA RECOVERY MODE ***
   LORA_OTA: Listening on op_channel for new firmware...
   ```

### Étape 2 : préparer la gateway

Le receiver Arduino doit être en mode "OTA gateway" (à implémenter dans son firmware).

**TODO** : ajouter dans le sketch Arduino du receiver une commande série :
- `OTA_GW_INIT <mac_hex>` — passe en mode OTA, mémorise le MAC cible
- `OTA_TX <hex_packet>` — chiffre le paquet avec la clé du MAC, transmet via LoRa, attend ACK, retourne `OTA_RX <ack_hex>` ou `OTA_TIMEOUT`

### Étape 3 : lancer l'OTA

```bash
python ota_send.py firmware.bin --port COM5 --target-mac 00158D0005D2ABE1 --new-version 0x0102
```

Sortie attendue :
```
============================================================
ZLinky OTA sender
============================================================
Firmware     : ZLinky_GpProxy.bin
Size         : 196880 bytes
CRC32        : 0x7D6EFAA6
Version      : 0x0102
Block size   : 64 bytes
Total blocks : 3077
Target MAC   : 00158D0005D2ABE1
Port         : COM5 @ 115200
Estimated transfer time : 12.8 min @ SF11 (250 ms/block)
============================================================

[1/3] Sending OTA_START...
  OK : ZLinky ready to receive 3077 blocks

[2/3] Sending 3077 blocks...
  block 50/3077  1.6%  4.0 blk/s  ETA 760s
  block 100/3077  3.2%  4.0 blk/s  ETA 745s
  ...
  All blocks sent in 765.3s (4.0 blk/s)

[3/3] Sending OTA_END...
  OK : ZLinky committing + rebooting now

*** OTA UPDATE COMPLETE ***
```

Le ZLinky redémarre automatiquement sur le nouveau firmware (slot B). Le bootloader NXP gère le swap.

## Sécurité

- **Confidentialité** : tous les paquets chiffrés AES-CTR avec la clé du pairing
- **Authenticité** : MIC CMAC — seul ton gateway (qui connaît la clé) peut envoyer un firmware accepté
- **Intégrité** : CRC32 par paquet (LoRa) + CRC32 global du firmware
- **Anti-rollback** : pas implémenté pour l'instant (TODO : refuser si version < courante)

## Dépannage

### "ERROR: gateway setup failed"
Le receiver n'est pas en mode OTA gateway, ou mauvais port.

### Beaucoup de "block N retry" / timeouts
Signal LoRa faible. Solutions :
- Approcher le ZLinky de la gateway (jusqu'à 1 m si possible pour test)
- Augmenter la TX power du gateway

### Le ZLinky ne semble pas en mode OTA
- Vérifier que tu maintiens bien le bouton AVANT et PENDANT le boot
- Logs UART : doit apparaître `LORA_OTA: *** OTA RECOVERY MODE ***`

### L'OTA s'arrête en cours
Pas de resume disponible (par design, on jette tout). Solutions :
- Power-cycle le ZLinky (l'ancien firmware reste actif)
- Relance l'OTA depuis le début

## Phase 7 receiver (à faire)

Pour tester end-to-end, il faut implémenter dans le receiver Arduino :

```cpp
// Commandes série OTA gateway
if (cmd == "OTA_GW_INIT") {
    // parse MAC hex, charger la clé associée depuis EEPROM
    // mode = OTA_GATEWAY
    Serial.println("OTA_GW_READY");
}
else if (cmd == "OTA_TX") {
    // parse hex packet
    // chiffrer avec aes128_ctr_crypt + cmac MIC
    // radio.transmit(encrypted_pkt, len)
    // radio.startReceive() avec timeout
    // attendre RX_DONE
    // dechiffrer + verifier MIC
    // Serial.print("OTA_RX "); Serial.println(decrypted_hex);
}
```

C'est ~150 lignes de code à ajouter au sketch existant.

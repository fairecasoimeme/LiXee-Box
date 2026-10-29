#pragma once

// ============================================================================
// Suppression locale d'un appareil Zigbee, commune a la route web
// POST /api/zigbee/delete et au depart d'un appareil (trame 0x8048, callback
// enregistre par zigate_register_device_leave_cb).
//
// Retire l'endpoint Matter, les donnees de l'appareil (device_data et
// /littlefs/db/<IEEE>.json), la copie /littlefs/bk/<IEEE>.json et l'entree de
// la base ZiGate. Sans effet si l'appareil est deja supprime : les deux
// chemins peuvent s'executer pour le meme appareil.
//
// A appeler UNIQUEMENT sur la tache "zigate_proc" : soit depuis un callback
// ZiGate (c'est le cas du callback de depart), soit via
// zigate_run_on_proc(). Les autres taches tiennent des pointeurs vers ces
// donnees ; c'est sur zigate_proc que les rapports d'attributs les modifient.
// ============================================================================

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void web_zigbee_remove_device_local(uint64_t ieee);

#ifdef __cplusplus
}
#endif

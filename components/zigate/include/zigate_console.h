#pragma once

// ============================================================================
// ZiGate Console - Journal circulaire des trames echangees avec la ZiGate
//
// Equivalent de la "console" de la LiXee-Box (/logs, /getLogBuffer,
// /cmdClearConsole). Sur la LiXee-Box ce tampon etait desactive (logPrint()
// renvoyait une chaine vide) ; ici il est reellement alimente : chaque trame
// emise (">") ou recue ("<") est ajoutee sous forme d'une ligne de texte
// horodatee, avec son code, sa longueur et ses premiers octets en hexa.
//
// Le tampon (~40 Ko) est alloue en PSRAM au premier usage. Lecture
// incrementale par numero de sequence : le client redemande "depuis la
// derniere ligne recue" sans recevoir deux fois la meme ligne.
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "zigate_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZIGATE_CONSOLE_LINES     256
#define ZIGATE_CONSOLE_LINE_LEN  152

/** Cree le tampon et son verrou. Appele par zigate_init(). */
void zigate_console_init(void);

/** Ajoute une trame (dir = '>' emise, '<' recue). */
void zigate_console_frame(char dir, const zigate_packet_t *pkt);

/** Ajoute une ligne de texte libre (evenement du pilote). */
void zigate_console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** Vide le journal (les numeros de sequence continuent de croitre). */
void zigate_console_clear(void);

/**
 * Appelle cb() pour chaque ligne de numero > since, de la plus ancienne a la
 * plus recente, au plus max_lines lignes (0 = toutes). Le verrou est tenu
 * pendant les appels : cb() doit etre bref et ne pas rappeler la console.
 * @return numero de la derniere ligne existante (a repasser en 'since')
 */
typedef void (*zigate_console_cb_t)(uint32_t seq, const char *line, void *ctx);
uint32_t zigate_console_foreach(uint32_t since, size_t max_lines,
                                zigate_console_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif

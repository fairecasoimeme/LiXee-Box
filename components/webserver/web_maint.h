#pragma once

#include "esp_http_server.h"

/**
 * Routes de maintenance (lot 6 du portage LiXee-Box : sauvegarde,
 * restauration, mise a jour du firmware). Deux handlers generiques
 * (GET et POST "/api/maint/" + joker) aiguillent vers les fonctions ci-dessous,
 * pour ne consommer que 2 des 64 entrees de max_uri_handlers.
 *
 * GET  /api/maint/status                   etat (canaux manual/auto, reboot, app)
 * POST /api/maint/status/reset             efface les messages (/resetUpdateStatus)
 * GET  /api/maint/backup/list              sauvegardes de /bk (.tar, .json)
 * POST /api/maint/backup/create            cree bk/backup.tar (/createBackupFile)
 * GET  /api/maint/backup/download?name=    telecharge bk/<name> (&inline=1 : affichage)
 * GET  /api/maint/backup/export            archive produite a la volee (rien en flash)
 * POST /api/maint/backup/delete            {"name"}            (/deleteBackupFile)
 * POST /api/maint/backup/restore-json      {"name"}            (/restoreBkFile)
 * POST /api/maint/restore                  corps brut = .tar   (/doRestore)
 * POST /api/maint/firmware                 corps brut = .bin   (/doUpdate)
 * POST /api/maint/restore/init|chunk|finish   par morceaux base64 (/restoreInit...)
 * POST /api/maint/firmware/init|chunk|finish  par morceaux base64 (/fwUpdateInit...)
 * POST /api/maint/abort                    annule l'operation en cours
 * GET  /api/maint/auto/config              URL de mise a jour automatique
 * POST /api/maint/auto/config              {"updateUrl","releaseInfoUrl"} (mode developpeur)
 * POST /api/maint/auto/start               telechargement + installation (/downloadUpdate)
 */
void web_maint_register(httpd_handle_t server);

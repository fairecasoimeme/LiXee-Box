#pragma once
// ============================================================================
// Configuration du projet pour la pile Matter (CONFIG_CHIP_PROJECT_CONFIG)
//
// Identite du pont dans le cluster Basic Information. Sans donnees d'usine
// (CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER desactive), la pile publiait ses
// valeurs par defaut "TEST_VENDOR" / "TEST_PRODUCT", affichees par Home
// Assistant et Apple Maison. La version (SoftwareVersionString) est celle de
// l'application (PROJECT_VER du CMakeLists racine).
// ============================================================================

#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME   "LiXee"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME  "LiXee_MatterBridge"

// NVS (issue #107) : la partition de 20 Ko est partagee avec le WiFi, la
// calibration radio et la config. La pile garde par defaut 3 reprises de
// session CASE par fabric (15, deux cles chacune) : une par fabric suffit,
// un controleur dont la reprise a ete evincee refait une ouverture complete.
// Les donnees d'un cache plus grand sont effacees au demarrage
// (matter_bridge.cpp, purge_session_resumption).
#define CHIP_CONFIG_CASE_SESSION_RESUME_CACHE_SIZE CHIP_CONFIG_MAX_FABRICS

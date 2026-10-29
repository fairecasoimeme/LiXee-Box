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

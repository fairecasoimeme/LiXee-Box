# LiXee MatterBridge - Projet ESP-IDF
# Pont Matter <-> Zigbee via ZiGate+
# Cible : ESP32-S3 (ou ESP32-P4)
#
# Build :
#   idf.py set-target esp32s3
#   idf.py build
#   idf.py -p COMx flash monitor

cmake_minimum_required(VERSION 3.16)

# Composants locaux du projet (zigate, matter_bridge, webserver)
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# cJSON : imbrication limitee a 32 niveaux (1000 par defaut). L'analyse est
# recursive : quelques centaines de "[" suffisaient a depasser la pile de 8 Ko
# du serveur web, y compris sur /login, sans authentification. La limite vaut
# pour tous les cJSON_Parse du projet ; aucun document legitime n'en approche.
idf_build_set_property(COMPILE_DEFINITIONS "CJSON_NESTING_LIMIT=32" APPEND)

# Version de l application (esp_app_desc), controlee par la mise a jour OTA
set(PROJECT_VER "3.0")
project(lixee_matter_bridge)

# Fichiers web : sources editables dans web_src/, compressees (gzip) vers
# data/web/*.gz avant chaque image LittleFS. Le serveur sert la version .gz.
add_custom_target(web_gz
    COMMAND ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/tools/build_web.py
    COMMENT "Compression des fichiers web (web_src -> data/web)"
    VERBATIM)

# Créer l'image LittleFS à partir du dossier data/
# Les fichiers seront flashés dans la partition "littlefs"
# Structure attendue par web_server.cpp : /littlefs/web/index.html(.gz), /littlefs/web/css/, etc.
littlefs_create_partition_image(littlefs ${CMAKE_CURRENT_LIST_DIR}/data FLASH_IN_PROJECT DEPENDS web_gz)

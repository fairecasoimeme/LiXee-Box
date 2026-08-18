void lixeeClusterManage(String inifile, int attribute,uint8_t datatype,int len, char* datas);

// Le cache deviceId -> DeviceData* de findDevice() est construit une fois puis fige. Il DOIT
// etre invalide a chaque ajout ou suppression dans `devices`, sinon un appareil appaire a chaud
// reste introuvable (ses attributs FF66 ne sont pas stockes) et un appareil supprime laisse un
// pointeur pendant. A appeler apres toute mutation de `devices`.
void invalidateDeviceCache();

// Publication MQTT des tarifs et couleurs Linky décodés
void publishLinkyTariffInfo(const String& deviceId);

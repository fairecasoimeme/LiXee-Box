void SimpleMeterManage(String inifile,int attribute,uint8_t datatype,int len, char* datas);

class DeviceData;
// Vrai si le compteur est en Historique option BASE (PTEC "TH.."). Dans ce cas il n'existe
// AUCUN index tarifaire : l'index total est range dans l'emplacement 256.
bool isHistoBaseOption(DeviceData* device);
bool isZLinkyHistoBaseOption();   // idem, pour le ZLinky designe dans Config -> Energie

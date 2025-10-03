void getBind(uint64_t mac, int device_id, String model);
void getConfigReport(uint8_t shortAddr[2], int device_id, String model);
bool getPollingDevice(uint8_t shortAddr[2], int device_id, String model);
void SpecificTreatment(uint8_t shortAddr[2], int endpoint, String model);
PSRAMString getLinkyDatas(String IEEE);
String getLinkyMode(int mode);
String getPowerGaugeAbo(String IEEE, String Attribute, String Time);
String getLastValuePower(String IEEE,String Attribute, String Time);
PSRAMString getTrendPower(String IEEE,String Attribute, String Time);
PSRAMString getDatasPower(String IEEE,String Attribute, String Time);
int getkWhBudget(String IEEE, String Time, int budget);

String   getTotalEnergy(String IEEE, String Time);
String getTrendEnergyEuros(String IEEE);
String getPowerGaugeTimeDay(String IEEE, String Attribute);
String getPowerDatas( String IEEE, String type, String Attribute, String time);
float getTarif(int attribute, String Type);
float getTarifPower(String IEEE, int power);
String getZigbeeValue(String IEEE, String cluster, String attribute);
String getDeviceStatus(String IEEE);

String getValuekWh(long energy);


void SendBind(uint64_t mac, int cluster);
void SendDeleteDevice(uint64_t mac);
void SendConfigReport(uint8_t shortAddr[2], int cluster, int attribut, int type, int rmin, int rmax, int rtimeout, uint8_t rchange);
void SendAttributeRead(int shortAddr, int endpoint, int cluster, int attribut);
void SendAction(int command, int ShortAddr, int endpoint, String tmpValue);
void SendActiveRequest(uint8_t shortAddr[2]);
void SendSimpleDescriptionRequest(uint8_t shortAddr[2], uint8_t endpoint);
void SendBasicDescription(uint8_t shortAddr[2], uint8_t endpoint);
void sendOtaLoadNewImage(int u16ShortAddr, uint32_t u32FileIdentifier, uint16_t u16HeaderVersion, uint16_t u16HeaderLength, uint16_t u16HeaderControlField, uint16_t u16ManufacturerCode, uint16_t u16ImageType, uint32_t u32FileVersion, uint16_t u16StackVersion, uint8_t au8HeaderString[], uint32_t u32TotalImage, byte u8SecurityCredVersion, uint64_t u64UpgradeFileDest, uint16_t u16MinimumHwVersion, uint16_t u16MaxHwVersion);
void sendOtaBlock( int u16ShortAddr, byte u8SeqNbr, byte u8Status, uint32_t u32FileOffset, uint32_t u32FileVersion, uint16_t u16ImageType, uint16_t u16ManuCode, byte u8DataSize, uint8_t au8Data[]);
void sendOtaSetWaitForDataParams( int u16TargetAddr, byte u8SeqNbr, byte u8Status, uint32_t u32CurrentTime, uint32_t u32RequestTime, uint16_t u16BlockDelay);
void sendOtaEndResponse( int u16ShortAddr, byte u8SeqNbr, uint32_t u32UpgradeTime, uint32_t u32CurrentTime, uint32_t u32FileVersion, uint16_t u16ImageType, uint16_t u16ManuCode);
void sendOtaImageNotify( int u16ShortAddr, byte u8NotifyType, uint32_t u32FileVersion, uint16_t u16ImageType, uint16_t u16ManuCode, byte u8Jitter);


//void readZigbeeDatas(uint8_t ShortAddr[2],uint8_t Cluster[2],uint8_t Attribute[2], uint8_t DataType,int len, char* datas);
void readZigbeeDatas(String filename,uint8_t Cluster[2],uint8_t Attribute[2], uint8_t DataType,int len, char* datas);

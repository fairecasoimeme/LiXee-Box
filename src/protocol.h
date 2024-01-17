#ifndef PROTOCOL_H_
#define PROTOCOL_H_
#include "config.h"

void protocolDatas(uint8_t datas[128], int DatasSize);
void datasManage(char packet[128],int count);
void DecodePayload(struct ZiGateProtocol protocol, int packetSize);
void sendPacket(Packet p);
void sendZigbeeCmd(Packet p);
void sendZigbeePacket(int cmd, int len, uint8_t datas[128]);
uint8_t getChecksum(int type, int len, uint8_t datas[256]);
void transcode(uint8_t c);

bool ScanDeviceToPoll();
bool ScanDeviceToRAZ();


String getMacAddress(uint8_t mac[9]);
String GetMacAdrr(int shortAddr);
void lastSeen(uint8_t ShortAddr[2]);
String GetSoftwareVersion(String inifile);

int GetShortAddr(String inifile);
int GetDeviceId(String inifile);
String GetNameStatus(int deviceId,String cluster, int attribut, String model);
String GetLastSeen(String inifile);
String GetLQI(String inifile);
String GetValueStatus(String inifile, int key, int attribut, String type, float coefficient, String unit);

IPAddress parse_ip_address(const char *str);

struct ZiGateProtocol {
  int type;
  int ln;
  uint8_t chksum;
  char payload[128];   
}; 

#endif

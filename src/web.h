Template * GetTemplate(int deviceId, String model);
bool TemplateExist(int deviceId);

String createEnergyGraph(String IEEE, String Type);

void initWebServer();
void webServerHandleClient();
void handleRoot();
void handleNotFound();
void handleSaveConfig();
void handleTools();
void handleLogs();
void handleReboot();
void handleUpdate();
void handleFSbrowser();
void handleReadfile();
void handleLogBuffer();
void handleScanNetwork();
void handleClearConsole();
void handleGetVersion();
void handleErasePDM();
void handleStartNwk();
void handlePermitJoin();
void handleReset();
void handleRawMode();
void handleRawModeOff();
void handleActiveReq();
void handleSetchipid();
void handleSetmodeprod();

void APIgetDevices();

void checkUpdateFirmware();
void runUpdateFirmware(uint8_t *data, size_t len);
void launchUpdateTask();

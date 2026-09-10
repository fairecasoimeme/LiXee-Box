void SendWindowCoveringAction(int shortaddr, int endpoint, String value);
void WindowCoveringManage(String inifile,int attribute,uint8_t datatype,int len, char* datas);

// Positionnement en pourcentage d'OUVERTURE (0 = ferme, 100 = ouvert).
// Deux codes de commande de template, car le sens de la mesure differe selon les appareils :
//   251 : appareil conforme ZCL -- la position ZCL mesure la FERMETURE, on convertit ;
//   252 : appareil qui inverse deja le sens de marche (ex. Tuya TS130F, dont la commande
//         UP est 1 au lieu de 0) -- la position lui est transmise telle quelle.
// Regle pratique : si l'action UP du template utilise la valeur 1, prendre 252 ; si elle
// utilise 0, prendre 251. A verifier en envoyant 100 % : le volet doit s'OUVRIR.
#define CMD_COVER_POSITION          251
#define CMD_COVER_POSITION_INVERTED 252
void SendWindowCoveringPosition(int shortaddr, int endpoint, int openPercent, bool inverted);

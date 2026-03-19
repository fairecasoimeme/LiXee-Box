# Tutoriel : Flasher la ZiGate de la LiXee-Box avec JN518x Programmer

Ce guide explique comment programmer le firmware d'une ZiGate (microcontroleur NXP JN5189) a l'aide de l'application **JN518x Programmer**.

## Prerequis

- Une **LiXee-Box** 
- Le **fichier firmware** (.bin) a programmer (ex: `ZiGatev2-324.bin`)
- L'application **JN518xProgrammerGUI.exe**
- Le driver FTDI installe (Windows le detecte generalement automatiquement)


---

## Etape 1 : Configurer la connexion

- Dévisser le boitier
- Positionner le switch sur FTDI
- Brancher la **LiXee-Box** sur le port USB de votre ordinateur sous Windows en maintenant le bouton flash puis relacher

![Etape 1](screenshots/flash_etape1.PNG)

Au lancement de l'application :

1. Cliquez sur **Refresh** pour détecter les ports série disponibles. Votre ZiGate apparait dans la liste (ex: `COM5`).
2. Sélectionnez la **vitesse de programmation** souhaitee. La vitesse de **1 000 000 baud** est recommandée pour un flash rapide. Si vous rencontrez des erreurs, réduisez a **115200**.
3. Cliquez sur **Connect** pour établir la connexion avec la ZiGate.

> **Note :** La ZiGate est automatiquement mise en mode bootloader par le driver FTDI. Aucune manipulation physique n'est necessaire.

---

## Etape 2 : Verifier la detection du chip

![Etape 2](screenshots/flash_etape2.PNG)

Apres connexion, l'application détecte automatiquement le microcontroleur :

- **Chip** : JN5189
- **MAC** : adresse MAC unique du module (ex: `00:15:8D:00:05:D2:4C:6A`)
- **Chip ID** : identifiant du chip (ex: `0X1140C686`)
- **Bootloader** : version du bootloader (ex: `0XCC000014`)

Le journal en bas de l'ecran confirme la connexion et le passage à la vitesse de programmation choisie.

> **Note :** Conservez votre adresse MAC si vous avez besoin de la retrouver plus tard.

---

## Etape 3 : Charger le firmware

![Etape 3](screenshots/flash_etape3.PNG)

1. Cliquez sur **Browse...** ou glissez-déposez votre fichier firmware dans le champ prévu. Le chemin du fichier s'affiche (ex: `C:/nxp/DK6Programmer/ZiGatev2-324.bin`), ainsi que sa taille et son point d'entree.
2. Cliquez sur **Program** pour lancer la programmation de la mémoire FLASH.

> **Astuce :** Laissez l'option **"Verify after programming"** cochée pour vérifier automatiquement l'intégrité du firmware après l'écriture.

---

## Etape 4 : Confirmer l'effacement

![Etape 4](screenshots/flash_etape4.PNG)

Une boite de dialogue de confirmation apparait avant l'effacement de la mémoire FLASH. Elle indique la zone mémoire qui sera éffacée (ex: `0x00000000` à `0x00049600`).

Cliquez sur **Yes** pour confirmer et démarrer l'opération.

> **Note :** Si l'option **"Force mode (skip confirmations)"** est cochée dans les paramètres, cette boite de dialogue n'apparait pas.

---

## Etape 5 : Programmation terminee

![Etape 5](screenshots/flash_etape5.PNG)

L'operation se deroule en trois phases : effacement, programmation, puis verification. La barre de progression atteint **100%** et le journal affiche **"Memory verified successfully"**.

Le firmware est maintenant installé sur votre ZiGate. Vous pouvez cliquer sur **Disconnect** puis 

- Repositionnez le switch sur ESP
- Revisser le boitier
- Débrancher et rébrancher le module pour qu'il démarre avec le nouveau firmware.

---

## En cas de probleme

| Probleme | Solution |
|----------|----------|
| Aucun port COM detecte | Verifiez que le driver FTDI est installe et que la ZiGate est branchee. Cliquez sur **Refresh**. |
| "Read error" a la connexion | Reessayez. Si le probleme persiste, debranchez et rebranchez la ZiGate. |
| Erreur pendant le flash | Reduisez la vitesse de programmation a **115200** et reessayez. |
| Echec de verification | Relancez la programmation. Si le probleme persiste, essayez un autre port USB. |

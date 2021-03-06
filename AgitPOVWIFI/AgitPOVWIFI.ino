// Version 2019-10-16

// COMMENT THE FOLLOWING LINE TO DEACTIVATE UDP DEBUGGING
// #include "udpDebug.h"

/*
   AgitPOV Wifi: 24-RGB LED dual sided POV with Wifi (ESP8266)
   (c) 2011-2019
   Contributors over the years
        Thomas Ouellet Fredericks - Debuging, Accelerometer, LED engine and animation code
        Alexandre Castonguay
        Mathieu Bouchard
        Alan Kwok
        Andre Girard andre@andre-girard.com
        Sofian Audry
        Mariangela Aponte Nuñez
        Jean-Pascal Bellemare
        Daniel Felipe Valencia dfvalen0223@gmail.com
        Alex Keeling
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "AgitPage.h"
#include <ESP8266WiFi.h> // Server // Thank you Ivan Grokhotkov 
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include "FS.h"     // pour écrire en mémoire avec SPIFFS,  FS.h - file system wrapper
///// De :  http://www.esp8266.com/viewtopic.php?f=32&t=5669&start=4#sthash.OFdvQdJF.dpuf ///
extern "C" { // Infos sur les clients connectés
#include<user_interface.h>
}

File record_f;

////////// Page web /////////
bool lecture = true;
String doc;

////////// ACCEL ////////////

#include <Math.h>
#include "FrameAccelerator.h"
FrameAccelerator frameAccelerator;
///////// FIN ACCEL /////////

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
ESP8266WebServer server(80);

////////// Leds //////////
#include "Leds.h"
Leds leds;

int colorId = 7; // see matching colors in Leds.h, 7 is RAINBOW

#define POV_ARRAY_MAX_SIZE 180

int povArray[POV_ARRAY_MAX_SIZE];
int povArrayLength = 0;

int inputIntColor = 0;

bool waitingForNewWord = false;

int previousFrameDisplayed = -1;
bool blanked;

//bool wheelModeWasTriggered = false;
//unsigned long wheelModeWasTriggeredTime = 0;

void setup(void) {

  Serial.begin(115200);

  leds.setup();

  leds.blank();

  Serial.println("Setuping frameAccelerator");
  frameAccelerator.setup();


  // MAC ADDRESS /////////////////////////

  // Do a little work to get a unique-ish name. Append the
  // last two bytes of the MAC (HEX'd) :
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String twoLastHexBytes = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                           String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  twoLastHexBytes.toLowerCase();
  String apNameString = "agitpov" + twoLastHexBytes;

  char apName[apNameString.length() + 1];
  memset(apName, 0, apNameString.length() + 1);

  for (int i = 0; i < apNameString.length()-1; i++)
    apName[i] = apNameString.charAt(i);

  // SETUP WIFI
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName, apName); //WiFi.softAP("AgitPOVXXX")
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);   // if DNSServer is started with "*" for domain name, it will reply with  // provided IP to all DNS request
    server.on("/", handleRoot);
  server.onNotFound(handleRoot);

  server.begin();
  Serial.println("Connect to http://192.168.4.1");

  SPIFFS.begin(); // initialise les opérations sur le système de fichiers
  // Serial.println("Please wait 30 secs for SPIFFS to be formatted");
  // SPIFFS.format(); // Besoin une seule fois pour formatter le système de fichiers // Wait 30 secs
  // Serial.println("Spiffs formatted");

  // eraseFiles();
  // ecrireFichier("AgitPOV1"); // pour programmer un mot


  // WAIT FOR CLIENTS
  int numberOfClients = 0;
  unsigned long keepServerOpenInterval = 45000; // ouvrir le serveur pendant 45 secondes
  unsigned long timeServerStarted = millis();

  while ( numberOfClients <= 0 && (millis() - timeServerStarted < keepServerOpenInterval) ) {
    frameAccelerator.update();
    if ( frameAccelerator.shaken(6) ) break;
    numberOfClients = getNumberOfClients();
    leds.nonBlockingOsXAnimation();
    yield();
  }

  // GOT A CONNEXION : WAIT TILL ITS CONCLUSION
  // QUIT IF THE CONNEXION IS LOST
  if ( numberOfClients > 0  ) waitingForNewWord = true ;

  //Serial.print("numberOfClients="); Serial.print(numberOfClients); Serial.print(" waitingForNewWord="); Serial.println(waitingForNewWord);
  while ( numberOfClients > 0 && waitingForNewWord ) {

    leds.nonBlockingRainbowAnimation();

    dnsServer.processNextRequest(); /// a-t-on une requête de connexion ?
    //Serial.println("setup() appelle handleclient()");
    server.handleClient();

    yield();

    numberOfClients = getNumberOfClients();
    //Serial.print("numberOfClients="); Serial.print(numberOfClients); Serial.print(" waitingForNewWord="); Serial.println(waitingForNewWord);
  }

#ifdef UDP_DEBUGING
  udp.begin(9999);
#else
  Serial.println("setup appelle turnItOff");
  turnItOff(); // fermeture du serveur
  Serial.println("setup a appelé turnItOff");
#endif
#ifdef SPIFFS_DEBUGGING
  int i=1; char filename[32]; 
  do {
    sprintf(filename,"/record%03d.txt",i++);
  } while (SPIFFS.exists(filename));
  record_f = SPIFFS.open(filename,"w");
#endif

  Serial.println("setup lireFichier"); lireFichier();

  leds.fill(colorId);

  Serial.println( "Fading");
  leds.blockingFadeOut(colorId, 1000);

  Serial.println("Good to go");

} ///// fin du setup

void blank() {

  if ( !blanked ) {
    blanked = true;
    leds.blank();
  }
}



void loop() {

  // Update accelerator values
  frameAccelerator.update();

  // MODE SELECTOR

    // 15 ou 16 // 16 ==  pas de 'bruit' mais on perd le mode 'shake'
  if (  frameAccelerator.y.range < 8 || frameAccelerator.x.average < -2) {

    // WHEEL MODEframeAccelerator.y.range
    if ( frameAccelerator.wheel(povArrayLength, POV_ARRAY_MAX_SIZE) ) {

      int frame = frameAccelerator.getFrame();
      if ( frame != previousFrameDisplayed) {
        previousFrameDisplayed = frame;
        // display         side a,          side b,                               with this colorId
        //leds.displayFrame( povArray[povArrayLength - frame - 1], povArray[frame] , colorId);
        leds.displayInversedFrame( povArray[frame], povArray[povArrayLength - frame - 1]  , colorId);
      }
      blanked = false;
    } else {
      blank();
    }

  } else  {

    // WAVE MODE

    //   bool wave(int frameCount, float threshold) return true of it is triggered
    if ( frameAccelerator.wave(povArrayLength, 2) ) {
      int frame = frameAccelerator.getFrame();
      if ( frame != previousFrameDisplayed) {
        previousFrameDisplayed = frame;
        // display         side a,          side b,                               with this colorId
        //leds.displayInversedFrame( povArray[frame], povArray[povArrayLength - frame - 1]  , colorId);
        leds.displayFrame( povArray[povArrayLength - frame - 1], povArray[frame] , colorId);
      }
      blanked = false;
    } else {
      blank();
    }
  }



} // fin du loop

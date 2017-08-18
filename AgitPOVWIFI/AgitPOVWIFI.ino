// Version 2017-08-16

// COMMENT THE FOLLOWING LINE TO DEACTIVATE UDP DEBUGGING

/*
   AgitPOV Wifi: 24-RGB LED dual sided POV with Wifi (ESP8266)

   (c) 2011-2017
   Contributors over the years

        Thomas Ouellet Fredericks - Debuging, Accelerometer, LED engine and animation code
        Alexandre Castonguay
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
  // last three bytes of the MAC (HEX'd) :
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String threeLastHexBytes = String(mac[WL_MAC_ADDR_LENGTH - 3], HEX) +
                             String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                             String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  threeLastHexBytes.toLowerCase();
  String apNameString = "agitpov" + threeLastHexBytes;

  char apName[apNameString.length() + 1];
  memset(apName, 0, apNameString.length() + 1);

  for (int i = 0; i < apNameString.length(); i++)
    apName[i] = apNameString.charAt(i);

  // SETUP WIFI
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName, apName); //WiFi.softAP("AgitPOVXXXXXX")
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

  while ( numberOfClients > 0 && waitingForNewWord ) {

    leds.nonBlockingRainbowAnimation();

    dnsServer.processNextRequest(); /// a-t-on une requête de connexion ?
    server.handleClient();

    yield();

    numberOfClients = getNumberOfClients();

  }

#else
  turnItOff(); // fermeture du serveur
#endif


  lireFichier();

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
  if (  frameAccelerator.y.range < 10 ) {

    // WHEEL MODE
    if ( frameAccelerator.wheel(povArrayLength, POV_ARRAY_MAX_SIZE) ) {

      int frame = frameAccelerator.getFrame();
      if ( frame != previousFrameDisplayed) {
        previousFrameDisplayed = frame;
        // display         side a,          side b,                               with this colorId
        leds.displayFrame( povArray[povArrayLength - frame - 1], povArray[frame] , colorId);
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
        leds.displayInversedFrame( povArray[frame], povArray[povArrayLength - frame - 1]  , colorId);
      }
      blanked = false;
    } else {
      blank();
    }
  }



} // fin du loop





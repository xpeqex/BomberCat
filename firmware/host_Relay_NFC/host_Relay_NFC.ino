/************************************************************
  Example for read NFC card via MQTT version for BomberCat
  by Andres Sabas, Electronic Cats (https://electroniccats.com/)
  by Salvador Mendoza (salmg.net)
  Date: 17/05/2022

  This example demonstrates how to use BomberCat by Electronic Cats


  Development environment specifics:
  IDE: Arduino 1.8.19
  Hardware Platform:
  BomberCat
  - RP2040

  Thanks Wallee for support this project open source https://en.wallee.com/

  Electronic Cats invests time and resources providing this open source code,
  please support Electronic Cats and open-source hardware by purchasing
  products from Electronic Cats!

  This code is beerware; if you see me (or any other Electronic Cats
  member) at the local, and you've found our code helpful,
  please buy us a round!
  Distributed as-is; no warranty is given.
  /************************************************************/

#include "arduino_secrets.h"
#include <ArduinoJson.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include "Electroniccats_PN7150.h"

#define DEBUG

// Update these with values suitable for your network.

const char* ssid = ssidName;
const char* password = passWIFI;
const char* mqtt_server = mqttServ;

const char* outTopic = "RelayHost";
const char* inTopic = "RelayClient";

// Create a client ID
String clientId = "BomberCatHost-CARD01";

#define L1         (LED_BUILTIN)  //LED1 indicates activity

#define NPIN       (5) //Button

WiFiClient espClient;
int status = WL_IDLE_STATUS;

PubSubClient client(espClient);
unsigned long lastMsg = 0;

#define PN7150_IRQ   (11)
#define PN7150_VEN   (13)
#define PN7150_ADDR  (0x28)

Electroniccats_PN7150 nfc(PN7150_IRQ, PN7150_VEN, PN7150_ADDR); // creates a global NFC device interface object, attached to pins 7 (IRQ) and 8 (VEN) and using the default I2C address 0x28
RfIntf_t RfInterface;

uint8_t mode = 2;                                                  // modes: 1 = Reader/ Writer, 2 = Emulation

uint8_t commandlarge = 0;

//Visa MSD emulation variables
uint8_t apdubuffer[255] = {}, apdulen;

uint8_t ppse[255];
boolean detectCardFlag = false;

/*****************
       NFC
 *****************/

void resetMode() { //Reset the configuration mode after each reading
  #ifdef DEBUG
  Serial.println("Reset...");
  #endif
  if (nfc.connectNCI()) { //Wake up the board
    Serial.println("Error while setting up the mode, check connections!");
    while (1);
  }

  if (nfc.ConfigureSettings()) {
    Serial.println("The Configure Settings failed!");
    while (1);
  }

  if (nfc.ConfigMode(mode)) { //Set up the configuration mode
    Serial.println("The Configure Mode failed!!");
    while (1);
  }

  nfc.StartDiscovery(mode); //NCI Discovery mode
}

//Print hex data buffer in format
void printBuf(const byte * data, const uint32_t numBytes) {
  uint32_t szPos;
  for (szPos = 0; szPos < numBytes; szPos++) {
    Serial.print(F("0x"));
    // Append leading 0 for small values
    if (data[szPos] <= 0xF)
      Serial.print(F("0"));

    Serial.print(data[szPos] & 0xff, HEX);
    if ((numBytes > 1) && (szPos != numBytes - 1)) {
      Serial.print(F(" "));
    }
  }
  Serial.println();
}

void printData(uint8_t *buff, uint8_t lenbuffer, uint8_t cmd) {
  char tmp[1];

  if (cmd == 1)
    Serial.print("\nCommand: ");
  else if (cmd == 2)
    Serial.print("\nReader command: ");
  else if (cmd == 3)
    Serial.print("\nHunter Cat answer: ");
  else
    Serial.print("\nCard answer: ");

  for (uint8_t i = 0; i < lenbuffer; i++) {
    Serial.print("0x");
    Serial.print(buff[i] < 16 ? "0" : "");
    Serial.print(buff[i], HEX);
    Serial.print(" ");
  }

  Serial.println();
}

//Find Track 2 in the NFC reading transaction
void seekTrack2() {
  #ifdef DEBUG
  Serial.println("Send data to Card...");
  #endif
  uint8_t apdubuffer[255] = {}, apdulen;

  //blink(L2, 150, 1);
  #ifdef DEBUG
  printData(ppse, commandlarge, 1);
  #endif

  // Send command from terminal to card
  nfc.CardModeSend(ppse, commandlarge);

  while (nfc.CardModeReceive(apdubuffer, &apdulen) != 0) { }

  if (nfc.CardModeReceive(apdubuffer, &apdulen) == 0) {
    #ifdef DEBUG
    printData(apdubuffer, apdulen, 4);
    #endif
    
    client.publish(outTopic, apdubuffer, apdulen);
  }
  else {
    Serial.println("Error reading the card!");
  }
}

//Is it a card in range? for Mifare and ISO cards
void detectcard() {
  while (detectCardFlag == false) {
    #ifdef DEBUG
    Serial.println("wait detect Card...");
    #endif
    if (!nfc.WaitForDiscoveryNotification(&RfInterface)) { // Waiting to detect cards

      if (RfInterface.ModeTech == MODE_POLL || RfInterface.ModeTech == TECH_PASSIVE_NFCA) {
        char tmp[16];
        #ifdef DEBUG
        Serial.print("\tSENS_RES = ");
        sprintf(tmp, "0x%.2X", RfInterface.Info.NFC_APP.SensRes[0]);
        Serial.print(tmp); Serial.print(" ");
        sprintf(tmp, "0x%.2X", RfInterface.Info.NFC_APP.SensRes[1]);
        Serial.print(tmp); Serial.println(" ");
        Serial.print("\tNFCID = ");
        printBuf(RfInterface.Info.NFC_APP.NfcId, RfInterface.Info.NFC_APP.NfcIdLen);
        #endif

        if (RfInterface.Info.NFC_APP.NfcIdLen != 4) {

          Serial.println("Ooops ... this doesn't seem to be a Mifare Classic card!");
          return;
        }

        if (RfInterface.Info.NFC_APP.SelResLen != 0) {
          #ifdef DEBUG
          Serial.print("\tSEL_RES = ");
          sprintf(tmp, "0x%.2X", RfInterface.Info.NFC_APP.SelRes[0]);
          Serial.print(tmp); Serial.println(" ");
          #endif
        }
      }
      switch (RfInterface.Protocol) {
        case PROT_ISODEP:

          #ifdef DEBUG
          Serial.println(" - Found ISODEP card");
          #endif

          seekTrack2();
          break;

        case PROT_MIFARE:
          Serial.println(" - Found MIFARE card");
          break;

        default:
          Serial.println(" - Not a valid card");
          break;
      }

      //* It can detect multiple cards at the same time if they use the same protocol
      /*if (RfInterface.MoreTags) {
        nfc.ReaderActivateNext(&RfInterface);
      }*/

      //* Wait for card removal
      //nfc.ProcessReaderMode(RfInterface, PRESENCE_CHECK);
      //Serial.println("CARD REMOVED!");

      //nfc.StopDiscovery();
      //nfc.StartDiscovery(mode);
      detectCardFlag = true;
    }
  }
}

//To read Mifare and Visa
void mifarevisa() {
  if(detectCardFlag == false){
    detectcard();
  }
  else{
    seekTrack2();
  }
  //detectCardFlag = false;
}

/*****************
       MQTT
 *****************/
//Callback MQTT suscribe to inTopic from RelayClient
void callback(char* topic, byte * payload, unsigned int length) {
  #ifdef DEBUG
  Serial.print("Host Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  #endif
  commandlarge = length;
  for (int i = 0; i < length; i++) {

    ppse[i] = payload[i];
    #ifdef DEBUG
    Serial.print(payload[i], HEX);
    #endif
  }
  #ifdef DEBUG
  Serial.println();
  #endif
  mifarevisa();
}
// Connect and reconnect to MQTT
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("status", "Hello I'm here RelayHost");
      // ... and resubscribe
      client.subscribe(inTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/*****************
       WIFI
 *****************/
void setup_wifi() {
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, password);

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

void blink(int pin, int msdelay, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(msdelay);
    digitalWrite(pin, LOW);
    delay(msdelay);
  }
}

void setup() {
  pinMode(L1, OUTPUT);
  pinMode(NPIN, INPUT_PULLUP);

  Serial.begin(9600);
  #ifdef DEBUG
  while (!Serial);
  #endif
  mode = 1;
  resetMode();

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // blink to show we started up
  blink(L1, 200, 6);

  Serial.println("BomberCat, yes Sir!");
  Serial.println("Host Relay NFC");
}

void loop() { // Main loop
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}

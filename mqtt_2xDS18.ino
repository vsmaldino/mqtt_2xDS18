#define fw_vers "2xDS18_0.1.0.0a"
// Firmware id  : 2xDS18
// Firmware vers: 0.1.0.0a a=autoupdate

// Attenzione che i pin SCL e SDA cambiano a seconda della scheda utilizzata Arduino UNO/Wemos D1R1/Wemos D1R2
//
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ESP8266httpUpdate.h>

#include "securities.h"
/*
 ******* securities.h ********
 * 
#define ssid "myssid"
#define password "mypass"
#define mqttServer "mymqttserver"
#define mqttPort myport
#define mqttUser "myusername"
#define mqttPassword "mypassword"
#define otahost "myotahost"
#define otaport myotaport
#define otapath "myotapath"
*/

#define maxRetr 20
#define mqttClientId "HomeBoiler"
#define mqttTopic "announcement/clientid"
#define mqttTopicOut    "home/boiler/wemosd1/out"
#define mqttTopicCmds   "home/boiler/wemosd1/cmds"
// ######## comandi ################
#define FORCEREAD "READNOW"
// #################################

// ######## stati ################
// i lampeggi di errore sono +2
#define statusOK 0
#define statusErrNoNet 1
#define statusErrNoMq 2
#define statusErrDallas 3
#define statusErrGen 4
// ###############################

#define checkMqttClient 80 // in ms intervallo per il controllo dell'arrivo di messaggi
#define leaseDuration 6 // lease del DHCP in hour 

#define readingSensorsInterval 3 // in minuti, ogni quanti minuti legge i sensori
#define minReadTime 30           // in sec, tempo minimo fra una lettura e l'altra
#define numReads 3 // numero di letture fra cui fare la media

#define ledSigPin LED_BUILTIN
#define myOFF HIGH // LED_BUILTIN funziona al contrario
#define myON  LOW  // LED_BUILTIN funziona al contrario
 
#define ONE_WIRE_BUS_pin D8

#define NumberOfErrorSignal 5 // numero di volte che segnala l'errore
#define delay1 300
#define delay2 1000
#define delay3 10

// Setup a oneWire instance to communicate with any OneWire device
OneWire oneWire(ONE_WIRE_BUS_pin);
// Pass oneWire reference to DallasTemperature library
DallasTemperature sensors(&oneWire);

// indirizzi estratti con oneWireSearch.ino
DeviceAddress dallasAddress[2] = {
  { 0x28, 0xFF, 0xB5, 0x7C, 0x85, 0x16, 0x04, 0x1F  },
  { 0x28, 0xFF, 0x97, 0x7B, 0x85, 0x16, 0x04, 0xE2  }
};
unsigned int OneWireDeviceCount=0;

int status; // stato del sistema

#define heartBeatTime 7 // in sec, ogni quanto accende la luce di heartbeat
                        // l'accensione dura circa 1/10 della pausa
boolean heartBeatStatus=false;
unsigned long loopHeartBeat; // cicli per il battito del cuore
unsigned long loopCount4HeartBeat=0;
unsigned long loopMinReadTime; // cicli minimo fra una lettura e l'altra
unsigned long loopReadingSensors; // cicli prima di leggere i sensori
unsigned long loopCount4ReadingSensors=0;
unsigned long loopLeaseDuration;
unsigned long loopCount4LeaseDuration=0; 
unsigned long loop4client; // ogni quanti cicli controlla l'arrivo di messaggi
unsigned long loopCount4client=0; // contatore per il controllo dell'arrivo dei messaggi
                           // usato anche per la segnazione in condizione di errore
boolean forceReadSensors=false;
boolean readingMessageSent=false;

float floatDataVal1; // valore di temperatura, usato per la media
float floatDataVal2; // valore di temperatura, usato per la media
#define LAST   0     // indica che è l'ultima letura e quindi invia al broker
#define MEDIUM 1     // indica che è una lettura intermedia

WiFiClient espClient;
PubSubClient client(espClient);

void checkOTAupdates() {
  String otaurl;
  
  otaurl = String(otaprotocol);
  otaurl = String(otaurl + "://");
  otaurl = String(otaurl + otahost);
  otaurl = String(otaurl + ":");
  otaurl = String(otaurl + otaport);
  otaurl = String(otaurl + otapath);
  Serial.print("Check fo update OTA URL: ");
  Serial.println(otaurl);
  ESPhttpUpdate.rebootOnUpdate(false);
  t_httpUpdate_return ret = ESPhttpUpdate.update(espClient, otaurl, fw_vers); 
  /*upload information only */
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s",
                    ESPhttpUpdate.getLastError(),
                    ESPhttpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      break;
    case HTTP_UPDATE_OK:
      // actually this branch is never activated because the board restarts immediately after update
      Serial.println("HTTP_UPDATE_OK");
      Serial.println("Restarting ....");
      delay(5000);
      ESP.restart();
      break;
  }
} // checkOTAupdates


int myconnect() {
   int retr;
   char textAnnounce[100];

   WiFi.mode(WIFI_STA); // importante, altrimenti va in modo sta+ap e crea un ssid di nome FaryLink
   WiFi.begin(ssid, password);
   Serial.println();
   Serial.println();
   Serial.print("Connecting to WiFi Network ");
   Serial.print(ssid);
   Serial.print(" ");
   retr=0;
   while ((WiFi.status() != WL_CONNECTED) && (retr<maxRetr)) {
      delay(delay2);
      Serial.print(".");
      retr++;
   }
   if (WiFi.status() != WL_CONNECTED) {
      Serial.print(" !!!Failed!!!");
      return statusErrNoNet;
   }
   Serial.println(" connected"); 
   Serial.print("IP address: ");
   Serial.println(WiFi.localIP());
   
   Serial.println("Waiting few seconds for network to be up ...");
   delay(delay2*3);
   checkOTAupdates();
   // ip update found, checkOTAupdates() restarts the board
   client.setCallback(callback);
   client.setServer(mqttServer, mqttPort);
   Serial.print("Connecting to MQTT server ");
   Serial.print(mqttServer);
   Serial.print(" as clientid '");
   Serial.print(mqttClientId);
   Serial.print("' ");
   retr=0;
   while ((!client.connected()) && (retr<maxRetr)) {
      if(client.connect(mqttClientId, mqttUser, mqttPassword )) {
         Serial.println("connected");
      }
      else {
        Serial.print(".");
        delay(delay2*2);
      }
      retr++;
   }
   if (!client.connected()) {
      Serial.println("!!!Failed to connect to MQTT broker!!!");
      Serial.print("Failed state ");
      Serial.println(client.state());
      return statusErrNoMq;
   }
   sprintf(textAnnounce,"Hello, here %s", mqttClientId);
   client.publish(mqttTopic,textAnnounce);
   client.subscribe(mqttTopicCmds);
   return statusOK;
} // myconnect


void mydisconnect () {
   client.disconnect();
   delay(delay2);
   WiFi.disconnect();
} // mydisconnect


void setup() {
   
   pinMode(ledSigPin, OUTPUT);
   digitalWrite(ledSigPin, myON);

   Serial.begin(19200);
   Serial.println();
   
   status=statusErrGen;
   
   loopCount4LeaseDuration=0;
   loopLeaseDuration=leaseDuration*3600*1000/delay3;
   
   loopCount4client=0;
   loop4client=checkMqttClient/delay3;
   
   loopCount4ReadingSensors=0;
   loopReadingSensors=readingSensorsInterval*60*990 /delay3; // 1% di compensazione tempo
   // Serial.print("loopReadingSensors :");
   // Serial.println(loopReadingSensors);

   loopHeartBeat=(heartBeatTime * 1000) /delay3;
   loopCount4HeartBeat=0;
   heartBeatStatus=false;
   
   loopMinReadTime=(minReadTime * 1000) / delay3;

   floatDataVal1=0;
   floatDataVal2=0;

   forceReadSensors=false;
   readingMessageSent=false;
    
   status=myconnect();

   if (status <= statusOK) {
      status=initSensors();
   }
} // setup


void analyzePayload(char *payload) {
  // client.publish(mqttTopic,"Light on");
  if (strstr(payload, FORCEREAD)) {
     // arrivata la richiesta di fare una lettura
     forceReadSensors=true;
     Serial.println("Richiesta di lettura");
  }
} // analyzePayload


void callback(char* topic, byte* payload, unsigned int length) {
  unsigned int i;
  Serial.print("Message received in topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  for(i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  analyzePayload((char *) payload);
  delay(delay1);
} // callback


void ledSignal(int lamps) {
  int i;

  // spegne il led
  digitalWrite(ledSigPin, myOFF);
  delay(delay2);
  for(i=0;i<lamps;i++) {
    digitalWrite(ledSigPin, myON);
    delay(delay1);
    digitalWrite(ledSigPin, myOFF);
    delay(delay1);
  }
} // ledSignal


void loop () {    
   loopCount4client++;
   if (status <= statusOK) {
      if (loopCount4client > loop4client) { 
        loopCount4client=0;
        // client.loop serve a inviare e recuperare i messaggi 
        // se il tempo fra 2 loop è troppo lungo, si perdono i
        // i messaggi, soprattutto quelli in ingresso
        client.loop();
      }
      
      loopCount4LeaseDuration++;
      if (loopCount4LeaseDuration > loopLeaseDuration) {
        loopCount4LeaseDuration=0;
        mydisconnect();
        ESP.restart();
      }
      
      loopCount4HeartBeat++;
      if (((loopCount4HeartBeat>(loopHeartBeat/10)) && (heartBeatStatus)) ||
          ((loopCount4HeartBeat>loopHeartBeat) && (!heartBeatStatus)))
      {
        loopCount4HeartBeat=0;
        if (heartBeatStatus) {
          digitalWrite(ledSigPin, myOFF);
          heartBeatStatus=false;
        }
        else {
          digitalWrite(ledSigPin, myON);
          heartBeatStatus=true;
        }
      }

      manageSensorReading();
      
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("No Wifi!!");
        mydisconnect();
        ESP.restart();
      }
      if (!client.connected()) {
        Serial.println("No MQTT broker!!!");
        mydisconnect();
        ESP.restart();
      }
      
      delay(delay3);
   }
   else {
      // condizione di errore
      ledSignal(status+2);
      delay(delay2*5);
      Serial.print("Condizione di errore, stato: ");
      Serial.println(status);
      // Serial.print(loopCount4client);
      // Serial.print(" ");
      // Serial.println(NumberOfErrorSignal);
      if (loopCount4client > NumberOfErrorSignal) {
        mydisconnect();
        ESP.restart();
      }
   }
} // loop


void readSensors(int phase) {
  char message[100];
  char topic[200];
  float localDataVal;
  
  // Serial.println("In readSensors");
  // Send the command to get temperatures
  sensors.requestTemperatures();
  // floatDataVal1=sensors.getTempCByIndex(0);
  localDataVal=sensors.getTempC(dallasAddress[1]);
  floatDataVal1+=localDataVal;
  // Serial.print(localDataVal); 
  // floatDataVal2=sensors.getTempCByIndex(1);
  localDataVal=sensors.getTempC(dallasAddress[0]);
  floatDataVal2+=localDataVal; 
  // Serial.print(" ");
  // Serial.println(localDataVal); 

  if (phase == LAST) {
    Serial.print("Temper. (dallas1) = ");
    Serial.print(floatDataVal1/numReads);
    Serial.println("*C");
    strcpy(topic, mqttTopicOut);
    strcat(topic,"/tempdallas1");
    sprintf(message,"%3.1f",floatDataVal1/numReads);
    client.publish(topic,message);
    
    Serial.print("Temper. (dallas2) = ");
    Serial.print(floatDataVal2/numReads);
    Serial.println("*C");
    strcpy(topic, mqttTopicOut);
    strcat(topic,"/tempdallas2");
    sprintf(message,"%3.1f",floatDataVal2/numReads);
    client.publish(topic,message);
  }
  Serial.println();
} // readSensors


int initSensors() {
   int retCode;

   sensors.begin(); // Start up the onewire bus for DS18B20
   // locate devices on the bus
   Serial.print("Locating devices ... ");
   Serial.print("found ");
   OneWireDeviceCount = sensors.getDeviceCount();
   Serial.print(OneWireDeviceCount, DEC);
   Serial.println(" devices.");
   Serial.println("");
   if (OneWireDeviceCount < 2) {
      Serial.println("Error! At least 2 sensors needed!");
      retCode=statusErrDallas;
   }
   else {
      retCode=statusOK;
   }
   return retCode;
} // initSensors


void manageSensorReading() {
  char topic[200];
  ++loopCount4ReadingSensors;
  if (loopCount4ReadingSensors > (loopReadingSensors - numReads + 1)) {
    // esegue la lettura dei sensori
    forceReadSensors=false; // disattiva eventuali richieste
    // Serial.println("Leggo i sensori");
    if (loopCount4ReadingSensors > loopReadingSensors) {
      readSensors(LAST);
      floatDataVal1=0;
      floatDataVal2=0;
      loopCount4ReadingSensors=0;
      // avvisa che è conclusa la sequenza di lettura
      strcpy(topic, mqttTopicOut);
      strcat(topic,"/reading");
      client.publish(topic,"READINGOFF");
      readingMessageSent=false;
    }
    else {
      // avvisa che è in corso la lettura
      if (!readingMessageSent) {
        strcpy(topic, mqttTopicOut);
        strcat(topic,"/reading");
        client.publish(topic,"READINGON");
        readingMessageSent=true;
      }
      readSensors(MEDIUM);
    }
  }
  else {
    if (forceReadSensors) {
      // avvisa che è pendente la richiesta di lettura
      if (!readingMessageSent) {
        strcpy(topic, mqttTopicOut);
        strcat(topic,"/reading");
        client.publish(topic,"READINGON");
        readingMessageSent=true;
      }
      if (loopCount4ReadingSensors > loopMinReadTime) {
        // forza il contatore per fare la lettura
        loopCount4ReadingSensors = loopReadingSensors - numReads;
        forceReadSensors=false; // disattiva la richiesta
      }
    }
  }
} // manageSensorReading

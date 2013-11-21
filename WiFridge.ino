#define ECHO_TO_SERIAL   0
#define DEBUG

#include <SPI.h>
#include <PString.h>
#include <avr/wdt.h>

// For Wifi
#include <WiFlyHQ.h>
#include <SoftwareSerial.h>

// For DS18B20 temp sensor
#include <OneWire.h>
#include <DallasTemperature.h>

// For AM2302 temp & humidity sensor
#include "DHT.h"

// WiFly HQ config
SoftwareSerial wifiSerial(7,8);
WiFly wifly;

char API_EMONCMS_PRIV[33];
char HOST_EMONCMS_PRIV[33];
char API_EMONCMS_LOCAL[33];
char HOST_EMONCMS_LOCAL[17];

//========================================================================================================
// Pin Configuration
//========================================================================================================

// IN
const char pinDS18B20 = 3;
const char pinAM2302 = 5;

// OUT
const int pinLedRed=10;
const int pinLedYellow=9;
const int pinLedGreen=6;

// CS / SS Pins for SD and Ethernet (they share the SPI bus)
const int chipSelectSD = 4;
const int chipSelectEth = 10;

// Buffers & Strings
char buf[16];

char payLoadBuf[255];
PString payLoad(payLoadBuf,sizeof(payLoadBuf));

char floatBuf[8];
int webErrCount=0;
int failedToSend=0;
char freeMem[6];

//========================================================================================================
// DS18B20 Temp sensor configuration
//========================================================================================================
OneWire oneWire(pinDS18B20);          // Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
DallasTemperature sensors(&oneWire);  // Pass our oneWire reference to Dallas Temperature. 
const int TempPrecision = 9;

//========================================================================================================
// AM2302 Temp sensor configuration
//========================================================================================================
DHT dht(pinAM2302, DHT22);

//========================================================================================================
// Update interval
//========================================================================================================
const unsigned long minIntervalTempRead = 60000;    // Interval between temp reads (in ms)
unsigned long lastTempRead=0;                       // last millis() Temp was read
unsigned long lastNtpRequest=0;                     // last millis() NTP request sent
unsigned long currentEpoch=0;
unsigned long readLoopDuration;

//========================================================================================================
void setup()
{
  Serial.begin(115200);
  
  Serial.println(F("===="));
  Serial.println(F("==== Booting up..."));
  Serial.println(F("===="));
  
  initVars();
  
  // It is necessary that both CS / SS pins are set to outputs
  pinMode(chipSelectSD, OUTPUT);
  pinMode(chipSelectEth, OUTPUT);
  
  pinMode(pinLedRed, OUTPUT);
  pinMode(pinLedYellow, OUTPUT);
  pinMode(pinLedGreen, OUTPUT);

  #if ECHO_TO_SERIAL
  Serial.println(F("========= Testing leds..."));
  #endif
  ledTrain();

  // -----------------------------------------------------------------------------------------------------
  // Start Wifi
  // -----------------------------------------------------------------------------------------------------
  #if ECHO_TO_SERIAL
  Serial.println(F("========= Setting up network..."));
  #endif
  
  wifiSerial.begin(9600);
  
  // If WiFly fails, reset the whole thing
  if (!wifly.begin(&wifiSerial, &Serial)) {
    Serial.println("Failed to start wifly");
    allLedBlink(3,1000);
    resetArduino();
  }

  // RN-XV is set for auto-association with my local Wifi
  // So we'll just wait a little while until it joined
  while(!wifly.isAssociated()) {
    Serial.println(F("============== Please wait while joining wifi network...")); 
    ledBlink(pinLedYellow,3,100);
    delay(200);
    ledBlink(pinLedYellow,3,100);
  }
  
  //#if ECHO_TO_SERIAL
  printWiFlyStatus();
  //#endif
  
  if (wifly.isConnected()) {
    #if ECHO_TO_SERIAL
    Serial.println(F("============== Old connection active. Closing"));
    #endif
    wifly.close();
  }

  #if ECHO_TO_SERIAL
  Serial.println(F("========= Initializing DS18B20"));
  #endif
  
  sensors.begin();                      

  #if ECHO_TO_SERIAL
  Serial.println(F("========= Sending boot flag"));
  #endif
  
  payLoad.print("WiFridgeStartup:1");
  sendData2EmonCms("/input/post.json",HOST_EMONCMS_PRIV,80,API_EMONCMS_PRIV,&payLoad);
  sendData2EmonCms("/emoncms/input/post.json",HOST_EMONCMS_LOCAL,80,API_EMONCMS_LOCAL,&payLoad);
  resetPayLoad(&payLoad);

  #if ECHO_TO_SERIAL
  Serial.println(F("========= Enabling Watchdog"));
  #endif
  
  wdt_enable(WDTO_8S);

  Serial.println(F("==== End of setup"));
  Serial.println();
}

//=======================================================================================================
void loop()
{
  wdt_reset();
  
  if(!lastTempRead || millis()-lastTempRead>minIntervalTempRead) {

    #if ECHO_TO_SERIAL
    Serial.println();
    Serial.println(F("==== "));
    #endif

    Serial.println(F("==== Read & send : Starting"));

    #if ECHO_TO_SERIAL
    Serial.println(F("==== "));
    printWiFlyStatus();
    #endif

    lastTempRead=millis();

    itoa(wifly.getFreeMemory(),freeMem,10);
    addAttr(&payLoad,"freeMemory",freeMem);
   
    digitalWrite(pinLedGreen,HIGH);

    // -----------------------------------------------------------------------------------------------------
    // Getting temp from DS18B20 and sending it to emoncms
    // -----------------------------------------------------------------------------------------------------
    #if ECHO_TO_SERIAL
    Serial.println(F("========= Reading 1-wire sensors..."));
    #endif
    sensors.requestTemperatures(); // Send the command to get temperatures
    
    dtostrf(sensors.getTempCByIndex(0), 0, 2, floatBuf);
    addAttr(&payLoad,"tempFridge",floatBuf);
    dtostrf(sensors.getTempCByIndex(1), 0, 2, floatBuf);
    addAttr(&payLoad,"tempFreezer",floatBuf);
    
    // -----------------------------------------------------------------------------------------------------
    // Getting temp & humidity from AM2302
    // -----------------------------------------------------------------------------------------------------
    #if ECHO_TO_SERIAL
    Serial.println(F("========= Reading AM2302 sensor..."));
    #endif
    
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    
    if(isnan(h)) { Serial.println(F("========= Could not read humidity")); } 
    else {
      dtostrf(h, 0, 2, floatBuf);
      addAttr(&payLoad,"humidityKitchen",floatBuf);
    }
    
    if(isnan(h)) { Serial.println(F("========= Could not read temp")); } 
    else {
      dtostrf(t, 0, 2, floatBuf);
      addAttr(&payLoad,"tempKitchen",floatBuf);
    }

    
    #if ECHO_TO_SERIAL 
    Serial.print(F("========= Data :")); Serial.println(payLoad);
    #endif
    
    digitalWrite(pinLedGreen,LOW);
    delay(100);
    digitalWrite(pinLedGreen,HIGH);
    
    Serial.println(F("========= Sending data"));
    
    sendData2EmonCms("/input/post.json",HOST_EMONCMS_PRIV,80,API_EMONCMS_PRIV,&payLoad);
    sendData2EmonCms("/emoncms/input/post.json",HOST_EMONCMS_LOCAL,80,API_EMONCMS_LOCAL,&payLoad);

    resetPayLoad(&payLoad);

    readLoopDuration=millis() - lastTempRead;
    Serial.print(F("==== Read & send : took "));
    Serial.println(readLoopDuration);
  }
  digitalWrite(pinLedGreen,LOW);
  
  if(webErrCount>10) resetArduino();  // Let's restart if too many errors

}

// -----------------------------------------------------------------------------------------------------
// Building pay load
// -----------------------------------------------------------------------------------------------------
void resetPayLoad(PString *payLoad) {
  payLoad->begin();
} 

void addAttr(PString *payLoad, char *attrName, char *attrValue) {

  if(payLoad->length()>0) { payLoad->print(F(",")); }
    
  payLoad->print(attrName);
  payLoad->print(F(":"));
  payLoad->print(attrValue);
}

// -----------------------------------------------------------------------------------------------------
// Sending data to EmonCms
// -----------------------------------------------------------------------------------------------------
void sendData2EmonCms(char *urlprefix,char host[],int port, char apikey[], PString *payLoad) {
  
  PROGMEM char HTTP_OK[]="HTTP/1.1 200 OK";
  PROGMEM char EMON_CMS_OK[]="ok";
  
  char httpRequestBuf[255];
  PString httpRequest(httpRequestBuf,sizeof(httpRequestBuf));
  httpRequest.begin();
  
  Serial.print(F("============== Sending data to "));
  Serial.print(host); Serial.print(F(":")); Serial.println(port);
  
  // if there's a successful connection:
  unsigned long sendStart=millis();
  if (wifly.open(host,port)) {
    #if ECHO_TO_SERIAL
    Serial.print(F("============== Connected"));
    #endif

    // send the HTTP PUT request:
    httpRequest.print(F("GET "));
    httpRequest.print(urlprefix);
    httpRequest.print(F("?apikey="));
    httpRequest.print(apikey);
    httpRequest.print(F("&json={"));
    httpRequest.print(*payLoad);
    httpRequest.println(F("} HTTP/1.0"));
    httpRequest.print(F("Host: "));
    httpRequest.println(host);
    httpRequest.println();

    #if ECHO_TO_SERIAL
    Serial.print(F("HTTP Request :"));
    Serial.println(httpRequest);
    #endif
    
    wifly.println(httpRequest);
    
    #if ECHO_TO_SERIAL
    Serial.println(F("============== Processing Answer"));
    #endif
    
    unsigned long timeout = 3000;
    
    // Looking for HTTP Ok
    if(wifly.match(HTTP_OK,timeout)) {
      #if ECHO_TO_SERIAL
      Serial.println(F("\n=================== Got a 200 ! Sor far, so good..."));
      #endif
      
      // Looking for emoncms Ok
      if(wifly.match(EMON_CMS_OK,timeout)) {
        #if ECHO_TO_SERIAL
        Serial.println(F("=================== Got ok from emoncms, everything is ok !"));;
        #endif
        digitalWrite(pinLedRed, LOW);
        webErrCount=0;
      }
      else {
        Serial.println(F("=================== emoncms did not reply properly"));
        digitalWrite(pinLedRed,HIGH);
        webErrCount++;
      }
    }
    else {
      Serial.println(F("=================== No HTTP 200. HTTP request failed"));
      digitalWrite(pinLedRed,HIGH);
      webErrCount++;
    }
  }
  else {
    // Couldn't make a connection...
    #if ECHO_TO_SERIAL
    Serial.println(F("=================== Could not even connect to emoncms..."));
    #endif
    //wifly.close();
    //rebootWifly();
    allLedBlink(5,50);
    webErrCount++;
  }    
  
  if(webErrCount>0) printWiFlyStatus();
  Serial.print(F("============== End of web request in "));
  Serial.print(millis() - sendStart); Serial.println();
}

// -----------------------------------------------------------------------------------------------------
// Reset Arduino
// -----------------------------------------------------------------------------------------------------
void resetArduino(void) {
  allLedBlink(5,200);
  wifly.reboot();
  delay(500);
  for(;;);
}

void rebootWifly(void) {
    int i;

    wifly.reboot();

    // waiting a bit so it connects to wifi
    for(i=0;i<10;i++) {
      allLedBlink(3,200);
      ledTrain();
    }
}

// -----------------------------------------------------------------------------------------------------
// Leds utilities
// -----------------------------------------------------------------------------------------------------
void ledBlink(int led,int count,int wait) {
  int i;
  for(i=0;i<count;i++) {
    digitalWrite(led,HIGH); delay(wait);
    digitalWrite(led,LOW); delay(wait);    
  }
}

void allLedBlink(int count,int wait) {
  int i;
  for(i=0;i<count;i++) {
    digitalWrite(pinLedRed, HIGH); digitalWrite(pinLedYellow, HIGH); digitalWrite(pinLedGreen, HIGH);
    delay(wait);
    digitalWrite(pinLedRed, LOW); digitalWrite(pinLedYellow, LOW); digitalWrite(pinLedGreen, LOW);
    delay(wait);    
  }
}


void ledTrain() {
    digitalWrite(pinLedRed, HIGH); delay(200);
    digitalWrite(pinLedYellow, HIGH); delay(200);
    digitalWrite(pinLedGreen, HIGH); delay(200);
    
    digitalWrite(pinLedRed,LOW); delay(200);
    digitalWrite(pinLedYellow, LOW); delay(200);
    digitalWrite(pinLedGreen, LOW); delay(200);
}

void printWiFlyStatus() {

  Serial.println(F("==== Network status"));
  Serial.print(F("SSID          : "));
  Serial.println(wifly.getSSID(buf,sizeof(buf)));
  Serial.print(F("IP            : "));
  Serial.println(wifly.getIP(buf,sizeof(buf)));

  Serial.print(F("Command mode  : "));
  Serial.println(wifly.isInCommandMode());

  Serial.print(F("Connected     : "));
  Serial.println(wifly.isConnected());
}



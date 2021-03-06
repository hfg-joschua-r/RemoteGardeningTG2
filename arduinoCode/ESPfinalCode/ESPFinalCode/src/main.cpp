#include <Arduino.h>
//STEPPER LIBRARY
#include <AccelStepper.h>

// Define motor interface type
#define motorInterfaceType 1
const int dirPin = D1;
const int stepPin = D2;
//WIFI CONNECTION
//Wichtig: WIFIconfig.h mit Wifi credintials updaten
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include "WIFIconfig.h"
#include <WiFiUdp.h>
#include <NTPClient.h>
//RFID READER
/*
 * Pin layout für unseren Reader:
 * -----------------------------------------------------------------------------------------
 *             MFRC522      Arduino       Esp   
 *             Reader/PCD   Uno/101       8266      
 * Signal      Pin          Pin           Pin       
 * -----------------------------------------------------------------------------------------
 * RST/Reset   RST          9             A0        
 * SPI SS      SDA(SS)      10            D0        
 * SPI MOSI    MOSI         11 / ICSP-4   D7        
 * SPI MISO    MISO         12 / ICSP-1   D6        
 * SPI SCK     SCK          13 / ICSP-3   D5        
 */

#include <MFRC522.h>
#define SS_PIN D0
#define RST_PIN A0

//TIMES
const long utcOffsetInSeconds = 7200;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
int lastWatered = 0;

// Timer: Wait for RFID-Reading
long prevTime = 0;
long timer = 300;

//HTTP client:
HTTPClient sender;
WiFiClient wifi;

MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the class
int rfidStatus = 2;            //Status um zu erkennen ob eine karte gescanned wurde
MFRC522::MIFARE_Key key;

// Init array that will store new NUID
byte nuidPICC[4];

//WASSER PUMPE
#define pumpPin D8
//endschalter
#define endSchalter D1
//backend IP entsprechend eurer IP anpassen
String backendIP = "http://192.168.178.170:3000";

//STEPPER
int stepdir = 0;
//AccelStepper stepper = AccelStepper(MotorInterfaceType, D1, D2, D3, D4);
AccelStepper stepper = AccelStepper(motorInterfaceType, stepPin, dirPin);
void calibration();

void setup()
{
  //Reader initiation
  Serial.begin(115200);
  SPI.begin();     // Init SPI bus
  rfid.PCD_Init(); // Init MFRC522

  for (byte i = 0; i < 6; i++)
  {
    key.keyByte[i] = 0xFF;
  }
  //Stepper setup
  stepper.setMaxSpeed(1000);
  stepper.setAcceleration(500);

  pinMode(pumpPin, OUTPUT); //PumpenPin definieren
  pinMode(endSchalter, INPUT); //endschalter als INPUT definieren
  //Mit WIFI verbinden
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Waiting for connection");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Sucessfully connected to ");
  Serial.println(WIFI_SSID);
  timeClient.begin();
  //Kalibrierung
  Serial.println("calibrating");
  calibration(); //funktion um den endschalter zu erreichen
}
//Funktion zum schalte der Pumpe für den gesetzten Amount an millisekunden
void waterPlant(int duration)
{
  Serial.print("Watering plant for ");
  Serial.print(duration);
  Serial.println(" milliseconds! :)");
  digitalWrite(pumpPin, HIGH);
  delay(duration);
  digitalWrite(pumpPin, LOW);
  Serial.println("Watered plant sucessfully");
  Serial.println(digitalRead(SS_PIN));
  rfidStatus = 0;
}
//neue Pflanze in unsere Datenbank hinzufügen
void addNewUIDToDB(String rfidUID)
{
  if (WiFi.status() == WL_CONNECTED)
  { //Check WiFi connection status
    Serial.println(rfidUID);
    WiFiClient client;
    HTTPClient http;
    // Ziel des Post-Requests
    String url = backendIP + "/plant/register";
    http.begin(client, url);
    // Den Content-Type für den Header definieren
    http.addHeader("Content-Type", "application/json");

    // JSON-String: {"uid": "031231"}
    String jsonString = "{\"uid\":\"" + rfidUID + "\",\"wifiName\": \"" + WIFI_SSID + "\"}";
    //String jsonString = "{\"uid\": \"" + rdifUID + "\,"wifiName\": \""+ WIFI_SSID + "\"}";
    int httpCode = http.POST(jsonString);
    // Antwort: Payload
    String payload = http.getString(); //Get the response payload

    // Ausgabe des Return Codes
    Serial.println(httpCode); //Print HTTP return code

    // Ausgabe des Antwort Payloads
    Serial.println(payload); //Print request response payload

    // Http_connection schließen
    http.end();
    rfidStatus = 0;
  }
  else
  {
    Serial.println("Error in WiFi connection");
  }
}
//Pflanzen infos abrufen (falls vorhanden)
void getPlantInfoDB(String rdifUID)
{
  if (WiFi.status() == WL_CONNECTED)
  { //Check WiFi connection status
    Serial.println(rdifUID);
    String url = backendIP + "/plant/status/" + rdifUID;
    Serial.println(url);
    sender.begin(wifi, url);

    // HTTP-Code der Response speichern
    int httpCode = sender.GET();
    Serial.println(httpCode);
    if (httpCode > 0)
    {
      // String vom request speichern
      String payload = sender.getString();
      // Ausgabe des Return Codes
      Serial.println(httpCode);
      if (httpCode == 200)
      {
        Serial.println(payload);
        //unsere Pflanze gießen für die ermittelte Zeit
        waterPlant(payload.toInt());
      }
      else if (httpCode == 404)
      {
        //Falls der RFID code nicht in unserer Datenbank existiert --> leg einen neuen Eintrag an
        addNewUIDToDB(rdifUID);
      }
    }
    else
    {
      // Falls HTTP-Error
      Serial.printf("HTTP-Error");
    }

    // Wenn alles abgeschlossen ist, wird die Verbindung wieder beendet
    sender.end();
    
  }
  else
  {
    //keine netzwerkverbindung
    Serial.println("Error in WiFi connection");
  }
}
//Funktion um RFID UID in DezimalZahl bzw String umzuwandeln, damit es für andere Funktionen brauchbar ist
void printDec(byte *buffer, byte bufferSize)
{
  String myString;
  for (byte i = 0; i < bufferSize; i++)
  {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], DEC);
    myString = myString + (String)buffer[i];
  }
  Serial.println();
  Serial.print("in String: ");
  Serial.println(myString);
  //Datenbank Abfrage nach Pflanzen Status/Gießbedarf
  getPlantInfoDB(myString);
  myString = ""; //String variable leeren
}
//Loop funktion um nach neuen RFID Tags zu suchen
void lookForNewRFIDUID()
{
  if (!rfid.PICC_IsNewCardPresent())
    return;

  // Verify if the NUID has been readed
  if (!rfid.PICC_ReadCardSerial())
    return;

  //Wenn wir eine neue Karte erkennen
  if (rfid.uid.uidByte[0] != nuidPICC[0] ||
      rfid.uid.uidByte[1] != nuidPICC[1] ||
      rfid.uid.uidByte[2] != nuidPICC[2] ||
      rfid.uid.uidByte[3] != nuidPICC[3])
  {
    Serial.println(F("A new card has been detected."));
    rfidStatus = 1; //pause stepper
    // Store NUID into nuidPICC array
    for (byte i = 0; i < 4; i++)
    {
      nuidPICC[i] = rfid.uid.uidByte[i];
    }

    Serial.println(F("The NUID tag is:"));
    Serial.println();
    Serial.print(F("In dec: "));
    printDec(rfid.uid.uidByte, rfid.uid.size);
    Serial.println();
  }
  else
    Serial.println(F("Card read previously."));

  // Halt PICC
  rfid.PICC_HaltA();

  // Stop encryption on PCD
  rfid.PCD_StopCrypto1();
}
void runStepper()
{
  // Run the motor forward at 500 steps/second until the motor reaches 1800 steps (4 revolutions):
  while (stepper.currentPosition() != -1800)
  {
    stepper.setSpeed(-500);
    stepper.runSpeed();
    yield();
  }
}
void calibration()
{
  //solange der endschalter nicht erreicht wird, fahr den stepper weiter.
  while (1)
  {
    if (digitalRead(endSchalter) == HIGH)
    {
      stepper.setSpeed(500);
      stepper.runSpeed();
      yield();
    }
    else
    {
      break;
    }
  }
}
//counter speichert wie oft die runStepper methode aufgerufen wurde
int counter = 0;
//maxRotations bestimmt wie oft wir runStepper maximal ausführen
int maxRotations = 700;
void loop()
{

  //uhrzeit abfragen
  timeClient.update();
  Serial.print("Es ist Stunde: ");
  Serial.println(timeClient.getHours());
  //derzeitigen system Status ausgeben
  Serial.print("Rfid status ist: ");
  Serial.println(rfidStatus);
  if (timeClient.getHours() == 15 || timeClient.getHours() == 18 && timeClient.getHours() != lastWatered)
  {
    rfidStatus = 0;
    stepper.setCurrentPosition(0);
    lastWatered = timeClient.getHours();
    //rfid status Kalibrierung
  }
  else
  {
    delay(5000);
  }

  if (rfidStatus == 0)
  {
    if (counter < maxRotations)
    {
      stepper.setCurrentPosition(0);
      runStepper();
      counter = counter + 1;
      prevTime = millis();
      Serial.print("currently at step: ");
      Serial.println(counter);
      while (1)
      {
        if (millis() > timer + prevTime)
        {
          break;
        }
        lookForNewRFIDUID();
      } 
    }
    else
    {
      Serial.println("Ende der gewindestange erreicht");
      rfidStatus = 2;
    }
  }
}
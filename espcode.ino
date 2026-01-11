/**
 * FINAL ESP32 CODE
 * Role: Main RFID Controller & Sender
 * Reads RFID, contacts server, controls indicators, and sends status to Arduino.
 * Baud Rate: 115200
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <HardwareSerial.h>

// Pin Definitions (VSPI)
#define RST_PIN 22 // RFID reset pin
#define SS_PIN 5   // RFID SDA pin (VSPI_SS)

// Pin Definitions for Indicators
#define GREEN_LED_PIN 25
#define RED_LED_PIN 26
#define BUZZER_PIN 27

// --- UPDATE THESE VALUES ---
const char* WIFI_SSID = "Xiaomi11i";
const char* WIFI_PASSWORD = "Pass(0)18";
const char* SCRIPT_BASE = "https://script.google.com/macros/s/AKfycbyEGUR1FRdfk7YMI5OBE2xLlGyZv86cK07ym2Ysjng2Kg0MwkFHF4OCLEy446Isw617/exec";
// -------------------------

const char* TOKEN = "RSP505";

// RFID Setup
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

// Serial object for communicating with Arduino (Serial2 uses GPIO 16, 17)
HardwareSerial espSerial(2);

// System Variables
String lastUID = "";
unsigned long lastCardRead = 0;
const unsigned long CARD_DEBOUNCE = 2000;
unsigned long lastWifiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000;

// Function Declarations
String urlEncode(const String &s);
bool httpGET(const String &url, String &resp, int &code);
void wifiConnect();
void readRFIDCard();
void handleRequest(const String &uid);
void signalSuccess();
void signalFailure();
void signalCardRead();

void setup() {
  Serial.begin(115200); // For debugging via USB
  delay(1000);

  // Initialize the serial port for Arduino communication
  // Uses GPIO 16 (RX2), 17 (TX2)
  espSerial.begin(115200, SERIAL_8N1, 16, 17);
  delay(100);

  // Initialize Indicator Pins
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Turn off all indicators initially
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("\n\n=== Industry - RFID Access Control & Tracking ===");
  SPI.begin(18, 19, 23, 5);
  mfrc522.PCD_Init();
  byte version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (version == 0x00 || version == 0xFF) {
    Serial.println("ERROR: RFID module not found!");
    digitalWrite(RED_LED_PIN, HIGH);
  } else {
    Serial.println("RFID module found and responding!");
  }
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);

  wifiConnect();
  Serial.println("System ready - waiting for employee cards...");
  espSerial.println("System Ready");
}

void loop() {
  if (mfrc522.PICC_IsNewCardPresent()) {
    if (mfrc522.PICC_ReadCardSerial()) {
      readRFIDCard();
      lastCardRead = millis();
    } else {
      Serial.println("Failed to read card serial");
      signalFailure();
    }
  }

  if (millis() - lastWifiCheck > WIFI_CHECK_INTERVAL) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, reconnecting...");
      wifiConnect();
    }
    lastWifiCheck = millis();
  }
  delay(100);
}

void handleRequest(const String &uid) {
  String url = String(SCRIPT_BASE) + "?mode=check_and_toggle&uid=" + urlEncode(uid) + "&token=" + urlEncode(TOKEN);
  String resp;
  int code;
  bool ok = httpGET(url, resp, code);

  if (ok) {
    Serial.println("Server response: " + resp);
    if (resp.indexOf("session_started") >= 0) {
      Serial.println("Session started successfully.");
      espSerial.println("Access Granted!"); 
      signalSuccess();
    } else if (resp.indexOf("session_ended") >= 0) {
      Serial.println("Session ended successfully.");
      espSerial.println("Session Ended");
      signalSuccess();
    } else {
      Serial.println("Unknown or denied response.");
      espSerial.println("Access Denied");
      signalFailure();
    }
  } else {
    Serial.println("Request failed: " + resp + " (Code: " + String(code) + ")");
    espSerial.println("Request Failed");
    signalFailure();
  }
}

void readRFIDCard() {
  String cardUID = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    cardUID += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    cardUID += String(mfrc522.uid.uidByte[i], HEX);
  }
  cardUID.toUpperCase();
  Serial.print("Employee UID: ");
  Serial.println(cardUID);

  if (cardUID == lastUID && (millis() - lastCardRead) < CARD_DEBOUNCE) {
    Serial.println("Card debounced - ignoring");
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  signalCardRead();
  Serial.println("Processing employee card...");
  handleRequest(cardUID);
  lastUID = cardUID;
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  Serial.println("Card processing complete");
}

void signalSuccess() {
  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
  delay(1800);
  digitalWrite(GREEN_LED_PIN, LOW);
}

void signalFailure() {
  digitalWrite(RED_LED_PIN, HIGH);
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
  delay(1500);
  digitalWrite(RED_LED_PIN, LOW);
}

void signalCardRead() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);
}

String urlEncode(const String &s) {
  String out;
  char c;
  for (size_t i = 0; i < s.length(); i++) {
    c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", (uint8_t)c);
      out += buf;
    }
  }
  return out;
}

bool httpGET(const String &url, String &resp, int &code) {
  if (WiFi.status() != WL_CONNECTED) {
    code = -1;
    resp = "no_wifi";
    return false;
  }
  HTTPClient http;
  http.begin(url);
  http.setTimeout(10000);
  code = http.GET();
  if (code == 302 || code == 301) {
    String newUrl = http.getLocation();
    http.end();
    http.begin(newUrl);
    http.setTimeout(10000);
    code = http.GET();
  }
  if (code > 0) {
    resp = http.getString();
  }
  http.end();
  return code == 200;
}

void wifiConnect() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long connectStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - connectStart) < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed!");
    digitalWrite(RED_LED_PIN, HIGH);
  }
}
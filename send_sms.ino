#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseESP8266.h>
#include <SPI.h>
#include <MFRC522.h>
#include "base64.h"
#include <time.h>

// RFID Pins
#define SS_PIN 4   // D2
#define RST_PIN 5  // D1
MFRC522 rfid(SS_PIN, RST_PIN);

// WiFi credentials
const char* ssid = "SIPU";
const char* password = "123123123";

// Firebase project credentials
#define API_KEY "AIzaSyBMe6vH24MY1H5b9nkpBlNFXI5UmRf_4b8"
#define DATABASE_URL "https://data-base1-aaca7-default-rtdb.asia-southeast1.firebasedatabase.app/"

// Twilio Credentials
const char* accountSid = "AC74903016543af5fa7bc5b53bd4573045";
const char* authToken = "a83c82611cff2b92d32c101875bc9082";
const char* twilioNumber = "+15312141006"; // Twilio number
const char* destinationNumber = "+919692909894"; // Your number

const char* host = "api.twilio.com";
const int httpsPort = 443;

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Function prototypes
void sendSMS(String message);
String getIndianTimestamp();
bool checkAuthorization(String uid);

void setup() {
  Serial.begin(115200);

  // Connect WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Connected to WiFi");

  // Sync NTP time
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");  // GMT+5:30
  Serial.println("Waiting for NTP time sync...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\n⏰ Time synchronized!");

  // Firebase Config
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // RFID Setup
  SPI.begin();
  rfid.PCD_Init();

  Serial.println("📶 Ready to scan RFID...");
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Read UID
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase(); // UID in capital letters

  Serial.print("Scanned UID: ");
  Serial.println(uid);

  bool authorized = checkAuthorization(uid);

  String timestamp = getIndianTimestamp();
  Serial.println("Timestamp: " + timestamp);

  if (authorized) {
    Serial.println("✅ Authorized UID");
  } else {
    Serial.println("❌ Unauthorized UID");
    sendSMS("❌ Unauthorized UID scanned: " + uid + " at " + timestamp);
  }

  delay(2000);

  rfid.PICC_HaltA();        
  rfid.PCD_StopCrypto1();   
}

// 🛡️ Check authorization function
bool checkAuthorization(String uid) {
  // Check in 'employees'
  if (Firebase.getString(fbdo, "/employees/" + uid)) {
    if (fbdo.dataType() == "string") {
      return true;
    }
  }

  // Check in 'product'
  if (Firebase.getString(fbdo, "/product/" + uid)) {
    if (fbdo.dataType() == "string") {
      return true;
    }
  }

  return false; // Not found in either table
}

// ⏰ Get current timestamp
String getIndianTimestamp() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buffer);
}

// 📩 Send SMS using Twilio
void sendSMS(String messageBody) {
  WiFiClientSecure client;
  client.setInsecure(); // Accept all SSL certificates

  Serial.print("Connecting to Twilio...");
  if (!client.connect(host, httpsPort)) {
    Serial.println("❌ Connection to Twilio failed");
    return;
  }
  Serial.println("✅ Connected to Twilio!");

  String credentials = String(accountSid) + ":" + String(authToken);
  String encoded = base64::encode(credentials);
  String auth = "Basic " + encoded;

  String url = "/2010-04-01/Accounts/" + String(accountSid) + "/Messages.json";
  String postData = "To=" + String(destinationNumber) + "&From=" + String(twilioNumber) + "&Body=" + messageBody;

  client.println("POST " + url + " HTTP/1.1");
  client.println("Host: " + String(host));
  client.println("Authorization: " + auth);
  client.println("Content-Type: application/x-www-form-urlencoded");
  client.println("Content-Length: " + String(postData.length()));
  client.println();
  client.println(postData);

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  Serial.println("📨 SMS request sent!");
  String response = client.readString();
  Serial.println(response);
}

#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <MFRC522.h>
#include "base64.h"

// Wi-Fi credentials
#define WIFI_SSID "JAGAN"
#define WIFI_PASSWORD "jagan2006"

// Firebase credentials
#define FIREBASE_HOST "https://data-base1-aaca7-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_SECRET "1Zlxvynb9BPenaq8dDVyOIx0Ib9Q2AID15EOwJPU"

// Twilio credentials
const char* accountSid = "AAC84c02711de342aad998810882e7fdb00";
const char* authToken = "1e8035116ad3d0a53ec76ca918d816c5";
const char* twilioNumber = "+15418240661";  // Twilio number
const char* destinationNumber = "+917735913554";

const char* host = "api.twilio.com";
const int httpsPort = 443;

// Define block order
const char* blockOrder[] = { "ENTRY BLOCK", "QUALITY CHECK", "LOCKER", "EXIT" };

// RFID setup
#define SS_PIN 4   // D2
#define RST_PIN 5  // D1
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Firebase setup
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
const String scannerLocation = "ENTRY BLOCK";  // <-- set fixed place for this scanner


// NTP time client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);  // GMT+5:30

// Function Prototypes
void sendSMS(String message);
bool checkAuthorization(String uid, String currentBlock);
void uploadToFirebase(String uid, String place, String timestamp);

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Connected");

  timeClient.begin();

  config.database_url = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_SECRET;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("✅ Firebase Initialized");
  Serial.println("Ready to scan RFID...");
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // Read UID
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  // Get current time
  timeClient.update();
  String timestamp = timeClient.getFormattedTime();

  // Determine current block based on scanner
  String currentBlock = scannerLocation;
// This should be set dynamically based on the scanner

  Serial.println("📟 UID: " + uid);
  Serial.println("🕒 Timestamp: " + timestamp);

  // Upload to /last_scanned
  uploadToFirebase(uid, currentBlock, timestamp);

  // Check if UID is authorized
  bool authorized = checkAuthorization(uid, currentBlock);

  if (!authorized) {
    Serial.println("❌ Unauthorized UID detected, sending SMS...");
    sendSMS("❌ Unauthorized UID scanned: " + uid + " at " + timestamp + " at " + currentBlock);
  } else {
    Serial.println("✅ Authorized UID");
  }

  mfrc522.PICC_HaltA();  // Halt card
  delay(2000);           // Avoid rapid re-scans
}

// 🛡️ Check authorization from Firebase
bool checkAuthorization(String uid, String currentBlock) {
  // Check if the UID is an employee
  if (Firebase.get(fbdo, "/employee/" + uid + "/place")) {
  String allowedPlace = fbdo.stringData();
  int allowedIndex = -1;
  int currentIndex = -1;

  for (int i = 0; i < sizeof(blockOrder) / sizeof(blockOrder[0]); i++) {
    if (String(blockOrder[i]) == allowedPlace) {
      allowedIndex = i;
    }
    if (String(blockOrder[i]) == currentBlock) {
      currentIndex = i;
    }
  }

  if (currentIndex != -1 && allowedIndex != -1 && currentIndex <= allowedIndex) {
    return true;
  }
}

  

  // Check if UID is a product (if needed)
  if (Firebase.get(fbdo, "/product/" + uid)) {
    if (fbdo.dataType() != "null") {
      return true;  // Authorized product
    }
  }

  return false;  // Not authorized
}
// 📩 Send SMS using Twilio
void sendSMS(String messageBody) {
  WiFiClientSecure client;
  client.setInsecure();  // Accept all SSL certificates

  Serial.print("Connecting to Twilio...");
  if (!client.connect(host, httpsPort)) {
    Serial.println("❌ Connection to Twilio failed");
    return;
  }
  Serial.println("✅ Connected to Twilio!");

  String credentials = String(accountSid) + ":" + String(authToken);
  String encoded = base64::encode(credentials);
  String authHeader = "Basic " + encoded;

  String url = "/2010-04-01/Accounts/" + String(accountSid) + "/Messages.json";
  String postData = "To=" + String(destinationNumber) + "&From=" + String(twilioNumber) + "&Body=" + messageBody;

  client.println("POST " + url + " HTTP/1.1");
  client.println("Host: " + String(host));
  client.println("Authorization: " + authHeader);
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

// Upload UID, place, and timestamp to Firebase
void uploadToFirebase(String uid, String place, String timestamp) {
  if (Firebase.setString(fbdo, "/last_scanned/uid", uid)) {
    Serial.println("✅ UID uploaded");
  } else {
    Serial.println("❌ UID upload failed: " + fbdo.errorReason());
  }

  if (Firebase.setString(fbdo, "/last_scanned/timestamp", timestamp)) {
    Serial.println("✅ Timestamp uploaded");
  } else {
    Serial.println("❌ Timestamp upload failed: " + fbdo.errorReason());
  }

  if (Firebase.setString(fbdo, "/last_scanned/place", place)) {
    Serial.println("✅ Place uploaded: " + String(place));
  } else {
    Serial.println("❌ Place upload failed: " + fbdo.errorReason());
  }
}
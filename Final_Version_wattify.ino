#include <WiFi.h>
#include <WebServer.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "EmonLib.h"
#include "time.h"

// ==== Firebase Credentials ====
#define API_KEY "AIzaSyBO3SFS4k8SxUq3poIGeH9Xgi98BDP1eeg"
#define DATABASE_URL "https://wattify001-default-rtdb.firebaseio.com/"
#define USER_EMAIL "wattifyofficial@gmail.com"
#define USER_PASSWORD "12345678"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
WebServer server(80);

const int builtInLED = 2;
const int relayPin = 32;
#define ZMPT_PIN 35
#define SCT_PIN  34

LiquidCrystal_I2C lcd(0x27, 16, 2);
EnergyMonitor emon1;
float voltageCal = 330.0;
float currentCal = 45.2;

String deviceUserName = "";
String wifiSSID = "";
String wifiPass = "";
bool credsReceived = false;
bool wasConnected = false;
bool isFirebaseInitialized = false;
unsigned long lastFirebaseCheck = 0;
unsigned long lastSensorUpdate = 0;

void handleRoot();
void handleFormSubmit();
float readACVoltage();
String getDateTime();
void startAP();

void setup() {
  Serial.begin(115200);
  pinMode(builtInLED, OUTPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(builtInLED, LOW);
  digitalWrite(relayPin, LOW);
  analogReadResolution(12);
  emon1.current(SCT_PIN, currentCal);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Wattify");
  lcd.setCursor(0, 1);
  lcd.print("Connecting...");

  startAP();
  configTime(18000, 0, "pool.ntp.org", "time.nist.gov"); // GMT+5 for Pakistan
}

void loop() {
  server.handleClient();

  if (credsReceived) {
    credsReceived = false;
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi Connected");
      lcd.setCursor(0, 1);
      lcd.print(WiFi.localIP());
      digitalWrite(builtInLED, HIGH);
      wasConnected = true;

      config.api_key = API_KEY;
      config.database_url = DATABASE_URL;
      auth.user.email = USER_EMAIL;
      auth.user.password = USER_PASSWORD;
      Firebase.reconnectNetwork(true);
      Firebase.begin(&config, &auth);
      isFirebaseInitialized = true;

      String path = "/wattify/" + wifiSSID + "_" + wifiPass + "_" + deviceUserName;
      Firebase.RTDB.setString(&fbdo, path + "/DeviceUserName", deviceUserName);
      Firebase.RTDB.setString(&fbdo, path + "/WiFiID", wifiSSID);
      Firebase.RTDB.setString(&fbdo, path + "/WiFiPassword", wifiPass);
      Firebase.RTDB.setString(&fbdo, path + "/relay_state", "high");
    } else {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi Failed");
      startAP();
    }
  }

  if (wasConnected && WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi Disconnected!");
    digitalWrite(builtInLED, LOW);
    digitalWrite(relayPin, LOW);
    wasConnected = false;
    startAP();
  }

  if (wasConnected && isFirebaseInitialized && millis() - lastFirebaseCheck > 1000) {
    lastFirebaseCheck = millis();
    String path = "/wattify/" + wifiSSID + "_" + wifiPass + "_" + deviceUserName + "/relay_state";
    String state;
    if (Firebase.RTDB.getString(&fbdo, path.c_str(), &state)) {
      Serial.println("relay_state: " + state);
      digitalWrite(relayPin, state == "high" ? HIGH : LOW);
    }
  }

  if (wasConnected && millis() - lastSensorUpdate > 1000) {
    lastSensorUpdate = millis();
    float voltage = readACVoltage() - 20.0;
    if (voltage < 0) voltage = 0;
    float current = emon1.calcIrms(1480);
    if (current < 1) current = 0;
    float power = voltage * current;
    float unitNow = (power / 3600000.0) * 1000.0;

    String timestamp = getDateTime();
    Serial.print("["); Serial.print(timestamp); Serial.print("] ");
    Serial.print("Voltage: "); Serial.print(voltage, 2); Serial.print(" V\t");
    Serial.print("Current: "); Serial.print(current, 2); Serial.print(" A\t");
    Serial.print("Power: "); Serial.print(power, 2); Serial.print(" W\t");
    Serial.print("Units: "); Serial.print(unitNow, 6); Serial.println(" kWh");

    lcd.setCursor(0, 0);
    lcd.print("V:"); lcd.print(voltage, 1);
    lcd.print(" I:"); lcd.print(current, 1);
    lcd.setCursor(0, 1);
    lcd.print("P:"); lcd.print(power, 1);
    lcd.print(" U:"); lcd.print(unitNow, 2);

    if (isFirebaseInitialized) {
      String path = "/wattify/" + wifiSSID + "_" + wifiPass + "_" + deviceUserName + "/Readings/" + timestamp;
      FirebaseJson json;
      json.set("timestamp", timestamp);
      json.set("voltage", voltage);
      json.set("current", current);
      json.set("power", power);
      json.set("units", unitNow);
      Firebase.RTDB.setJSON(&fbdo, path, &json);
    }
  }
}

void handleRoot() {
  server.send(200, "text/html", R"rawliteral(
    <!DOCTYPE html><html><head><title>Wattify - WiFi Setup</title>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
      body{font-family:Arial;text-align:center;padding:20px;}
      input{padding:10px;width:80%;margin:10px;}
      .container{max-width:400px;margin:auto;background:#f2f2f2;padding:20px;border-radius:10px;}
    </style></head><body>
    <div class='container'>
      <h2>Wattify</h2>
      <h4>WiFi Setup</h4>
      <form action='/submit' method='POST'>
        <input name='username' placeholder='Device User Name' required><br>
        <input name='ssid' placeholder='WiFi SSID' required><br>
        <input type='password' name='password' placeholder='WiFi Password' required><br>
        <button type='submit'>Connect</button>
      </form>
    </div></body></html>
  )rawliteral");
}

void handleFormSubmit() {
  if (server.hasArg("username") && server.hasArg("ssid") && server.hasArg("password")) {
    deviceUserName = server.arg("username");
    wifiSSID = server.arg("ssid");
    wifiPass = server.arg("password");

    Serial.println("Received WiFi credentials:");
    Serial.println("User: " + deviceUserName);
    Serial.println("SSID: " + wifiSSID);
    Serial.println("Pass: " + wifiPass);

    server.send(200, "text/html", "<h2>Connecting...</h2><p>Check WiFi LED status.</p>");
    credsReceived = true;
  } else {
    server.send(400, "text/plain", "Missing fields.");
  }
}

void startAP() {
  WiFi.softAP("Wattify-Setup", "12345678");
  Serial.println("Access Point Started: " + WiFi.softAPIP().toString());
  server.on("/", HTTP_GET, handleRoot);
  server.on("/submit", HTTP_POST, handleFormSubmit);
  server.begin();
}

float readACVoltage() {
  int samples = 1000;
  float maxVal = 0;
  float minVal = 4095;
  for (int i = 0; i < samples; i++) {
    int val = analogRead(ZMPT_PIN);
    if (val > maxVal) maxVal = val;
    if (val < minVal) minVal = val;
  }
  float peakToPeak = maxVal - minVal;
  float voltage = (peakToPeak * 3.3 / 4095.0) / 2.0;
  return voltage * voltageCal;
}

String getDateTime() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char buf[30];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d_%02d-%02d-%02d",
           timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
           timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  return String(buf);
}

#include <WiFi.h>
#include <WebServer.h>
#include <Firebase_ESP_Client.h>

// ==== Firebase Credentials ====
#define API_KEY "AIzaSyBO3SFS4k8SxUq3poIGeH9Xgi98BDP1eeg"
#define DATABASE_URL "https://wattify001-default-rtdb.firebaseio.com/"
#define USER_EMAIL "wattifyofficial@gmail.com"
#define USER_PASSWORD "12345678"

// ==== Firebase and WebServer ====
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
WebServer server(80);

// ==== Variables ====
const int ledPin = 2;
const int relayPin = 33;

String deviceUserName = "";
String wifiSSID = "";
String wifiPass = "";

bool credsReceived = false;
bool wasConnected = false;
unsigned long lastFirebaseCheck = 0;

// ==== Function Prototypes ====
void handleRoot();
void handleFormSubmit();

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
pinMode(relayPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  digitalWrite(relayPin, LOW);

  Serial.println("Wattify");
  delay(1000);
  Serial.println("Connecting...");

  // Start Access Point
  WiFi.softAP("Wattify-Setup", "12345678");
  Serial.print("Access Point IP Address: ");
  Serial.println(WiFi.softAPIP());

  // Setup Web Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/submit", HTTP_POST, handleFormSubmit);
  server.begin();
  Serial.println("✅ Web server started.");
}

void loop() {
  server.handleClient();

  if (credsReceived) {
    credsReceived = false;

    Serial.println("Connecting to WiFi...");
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }

 if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ WiFi Connected!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      digitalWrite(ledPin, HIGH);
      wasConnected = true;

      // Init Firebase
      config.api_key = API_KEY;
      config.database_url = DATABASE_URL;
      auth.user.email = USER_EMAIL;
      auth.user.password = USER_PASSWORD;
      Firebase.reconnectNetwork(true);
      Firebase.begin(&config, &auth);

      // Upload credentials to Firebase
      String path = "/wattify/" + wifiSSID + "" + wifiPass + "" + deviceUserName;
      Firebase.RTDB.setString(&fbdo, path + "/DeviceUserName", deviceUserName);
      Firebase.RTDB.setString(&fbdo, path + "/WiFiID", wifiSSID);
      Firebase.RTDB.setString(&fbdo, path + "/WiFiPassword", wifiPass);
    } else {
      Serial.println("\n❌ WiFi connection failed.");
      digitalWrite(ledPin, LOW);
      wasConnected = false;
    }
  }

  // Monitor WiFi disconnection
  if (wasConnected && WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi Disconnected!");
    digitalWrite(ledPin, LOW);
    digitalWrite(relayPin, LOW);
    wasConnected = false;
  }

  // === Firebase Check for Relay Control ===
  if (WiFi.status() == WL_CONNECTED && millis() - lastFirebaseCheck > 1000) {
    lastFirebaseCheck = millis();
    String path = "/wattify/" + wifiSSID + "" + wifiPass + "" + deviceUserName + "/relay_state";
    String state;

    Serial.println("Checking Firebase for relay_state...");
    Serial.println("Path: " + path);

    if (Firebase.RTDB.getString(&fbdo, path.c_str(), &state)) {
      Serial.println("relay_state: " + state);
      state.toLowerCase();
      digitalWrite(relayPin, state == "high" ? HIGH : LOW);
    } else {
      Serial.println("Firebase error: " + fbdo.errorReason());
    }
  }
}

// ==== Serve Web Form ====
void handleRoot() {
  Serial.println("Serving form page...");
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
  // ==== Handle Form Submit ====
void handleFormSubmit() {
  if (server.hasArg("username") && server.hasArg("ssid") && server.hasArg("password")) {
    deviceUserName = server.arg("username");
    wifiSSID = server.arg("ssid");
    wifiPass = server.arg("password");

    Serial.println("Received WiFi credentials:");
    Serial.println("User: " + deviceUserName);
    Serial.println("SSID: " + wifiSSID);
    Serial.println("Pass: " + wifiPass);

    server.send(200, "text/html", "<h2>Connecting...</h2><p>Check Serial Monitor.</p>");
    credsReceived = true;
  } else {
    server.send(400, "text/plain", "Missing fields.");
  }
}

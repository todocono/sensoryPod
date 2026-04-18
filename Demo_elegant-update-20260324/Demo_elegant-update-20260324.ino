
/*
  ElegantOTA - Access Point (Standalone WiFi) Example
*/

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
#else
  #include <WiFi.h>
  #include <WebServer.h>
#endif

#include <ElegantOTA.h>

// Set your desired Hotspot credentials
const char* ap_ssid = "ESP-OTA-Hotspot";
const char* ap_password = "password123"; // Must be at least 8 characters

#if defined(ESP8266)
  ESP8266WebServer server(80);
#else
  WebServer server(80);
#endif

void setup(void) {
  Serial.begin(115200);
  Serial.println("\nConfiguring Access Point...");

  // 1. Set mode to Access Point
  WiFi.mode(WIFI_AP);
  
  // 2. Start the Access Point
  // Syntax: WiFi.softAP(ssid, password)
  WiFi.softAP(ap_ssid, ap_password);

  // 3. Output the IP address
  // By default, ESP Access Points usually assign themselves 192.168.4.1
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", []() {
    server.send(200, "text/plain", "Connected to ESP Hotspot. Go to /update for OTA.");
  });

  // ElegantOTA Setup
  ElegantOTA.begin(&server); 
  
  server.begin();
  Serial.println("HTTP server started");
}

void loop(void) {
  server.handleClient();
  ElegantOTA.loop();
}
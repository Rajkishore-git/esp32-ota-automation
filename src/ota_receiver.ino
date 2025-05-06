#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <Update.h>

// 1) HardwareSerial for SIM800L
#define GSM_RX_PIN 18
#define GSM_TX_PIN 17
#define GSM_BAUD    9600

// 2) GPRS credentials
const char APN[]   = "airtelgprs.com";
const char USER[]  = "";
const char PASS[]  = "";

// 3) MQTT broker & topic (fill from mqtt_config.h)
#include "mqtt_config.h"

// 4) Globals
TinyGsm            modem(Serial2);
TinyGsmClient      netClient(modem);
PubSubClient       mqtt(netClient);

// forward
void performOta(const String &url);

void setup() {
  Serial.begin(115200);
  delay(500);

  // Bring up SIM800L
  Serial2.begin(GSM_BAUD, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
  delay(300);
  modem.restart();
  if (!modem.gprsConnect(APN, USER, PASS)) {
    Serial.println("GPRS connect failed");
    while (true) delay(1000);
  }
  Serial.println("GPRS up");

  // Setup MQTT
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback([](char* topic, byte* payload, unsigned int length) {
    // 1) build string
    String url;
    for (int i = 0; i < length; i++) url += (char)payload[i];
    Serial.print("OTA URL: "); Serial.println(url);
    // 2) launch OTA
    performOta(url);
  });

  // Connect & subscribe
  while (!mqtt.connected()) {
    Serial.print("MQTT connecting… ");
    if (mqtt.connect("esp32-s3-client")) {
      Serial.println("OK");
      mqtt.subscribe(MQTT_TOPIC);
    } else {
      Serial.print("fail, rc=");
      Serial.print(mqtt.state());
      delay(5000);
    }
  }
}

void loop() {
  // keep MQTT alive
  if (!mqtt.connected()) {
    // reconnect logic omitted for brevity
  }
  mqtt.loop();
}

// === OTA routine (based on your resume‑and‑retry code) ===
void performOta(const String &url) {
  // parse url into host, path
  String host, path;
  if (!url.startsWith("http://")) {
    Serial.println("Unsupported URL");
    return;
  }
  int idx = url.indexOf('/', 7);
  host = url.substring(7, idx);
  path = url.substring(idx);

  Serial.printf("Host: %s  Path: %s\n", host.c_str(), path.c_str());

  // 1) initial HEAD to get total length
  if (!netClient.connect(host.c_str(), 80)) {
    Serial.println("TCP connect failed"); return;
  }
  // send HEAD (or GET for byte 0)…
  netClient.print(String("GET ") + path + " HTTP/1.1\r\n" +
                  "Host: " + host + "\r\n" +
                  "Connection: close\r\n\r\n");
  // parse headers to find Content-Length…
  int totalLength = -1;
  {
    String line = netClient.readStringUntil('\n'); // status
    while (true) {
      line = netClient.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) break;
      if (line.startsWith("Content-Length:")) {
        totalLength = line.substring(15).toInt();
      }
    }
  }
  netClient.stop();
  if (totalLength <= 0) {
    Serial.println("Invalid length"); return;
  }
  Serial.printf("Size: %d\n", totalLength);

  // 2) begin OTA
  if (!Update.begin(totalLength)) {
    Serial.println("Not enough flash"); return;
  }

  // 3) download with Range resume…
  int bytesReceived = 0, retries = 0;
  const int maxRetries = 3;
  while (bytesReceived < totalLength && retries < maxRetries) {
    if (!netClient.connect(host.c_str(), 80)) {
      retries++; continue;
    }
    // send GET with Range
    netClient.print(String("GET ") + path + " HTTP/1.1\r\n" +
                    "Host: " + host + "\r\n" +
                    "Range: bytes=" + bytesReceived + "-\r\n" +
                    "Connection: close\r\n\r\n");
    // skip headers
    String line;
    while (true) {
      line = netClient.readStringUntil('\n');
      line.trim(); if (line.length()==0) break;
    }
    // read body
    unsigned long last = millis();
    while ((netClient.connected() || netClient.available()) && bytesReceived < totalLength) {
      if (netClient.available()) {
        uint8_t buf[2048];
        int toRead = min((int)sizeof(buf), totalLength - bytesReceived);
        int len = netClient.read(buf, toRead);
        Update.write(buf, len);
        bytesReceived += len;
        last = millis();
        Serial.printf("Got %d/%d\r", bytesReceived, totalLength);
      }
      if (millis() - last > 20000) {
        retries++; break;
      }
    }
    netClient.stop();
  }

  // 4) finalize
  bool ok = Update.end() && Update.isFinished();
  Serial.printf("\nOTA %s: %d/%d retries=%d\n",
                ok ? "Success" : "Failed",
                bytesReceived, totalLength, retries);
  if (ok) ESP.restart();
}

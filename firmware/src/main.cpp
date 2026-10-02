#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <HTTPClient.h>
#include "secrets.h"
#include "certs.h"

// Pins
#define SERVO_PIN   13
#define BUZZER_PIN  12
#define PIR_PIN     27
#define LDR_PIN     34
#define LED1_PIN    26   // LED1 > Door (motion + dark, fully automatic)
#define DHT_PIN     25
#define LED2_PIN    33   // LED2 > Room 1 (web app controlled)
#define LED3_PIN    32   // LED3 > Room 2 (web app controlled)
#define LED4_PIN    14   // LED4 > Room 3 (web app controlled)
#define LED5_PIN    23   // LED5 > Room 4 (web app controlled)

#define SERVO_LOCKED_ANGLE    0
#define SERVO_UNLOCKED_ANGLE  90

#define DHT_INTERVAL_MS 5000

// MQTT Topics
const char* TOPIC_PIN_ATTEMPT = "home/door/pin_attempt";
const char* TOPIC_DOOR_STATUS = "home/door/status";
const char* TOPIC_ALERTS      = "home/alerts";
const char* TOPIC_DHT         = "home/room/dht";
const char* TOPIC_R1_SET      = "home/lights/room1/set";
const char* TOPIC_R1_STATE    = "home/lights/room1/state";
const char* TOPIC_R2_SET      = "home/lights/room2/set";
const char* TOPIC_R2_STATE    = "home/lights/room2/state";
const char* TOPIC_R3_SET      = "home/lights/room3/set";
const char* TOPIC_R3_STATE    = "home/lights/room3/state";
const char* TOPIC_R4_SET      = "home/lights/room4/set";
const char* TOPIC_R4_STATE    = "home/lights/room4/state";

// Globals
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
Servo doorServo;
DHT dht(DHT_PIN, DHT11);

bool doorUnlocked = false;
unsigned long unlockedAt = 0;

bool doorLightOn = false;
unsigned long lastMotionMillis = 0;

unsigned long lastDHTRead = 0;

float lastTemp = NAN;
float lastHum = NAN;

bool thingSpeakPostedBefore = false;
unsigned long lastThingSpeakPost = 0;
bool doorEventPostedBefore = false;
unsigned long lastDoorEventPost = 0;

bool led2State = false, led3State = false, led4State = false, led5State = false;

// Declarations
void connectWiFi();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handlePinAttempt(String pin);
void unlockDoor();
void relockDoorIfNeeded();
void publishAlert(const char* type, const char* message);
void buzzWrongPin();
void buzzHighTemp();
void updateDoorLight();
void readAndPublishDHT();
void setRoomLED(int pin, String msg, const char* stateTopic, bool &state);
void publishToThingSpeak();
void logDoorEventToThingSpeak(bool success);

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  pinMode(LED4_PIN, OUTPUT);
  pinMode(LED5_PIN, OUTPUT);

  doorServo.setPeriodHertz(50);
  doorServo.attach(SERVO_PIN, 500, 2400);
  doorServo.write(SERVO_LOCKED_ANGLE);

  dht.begin();

  connectWiFi();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  connectMQTT();
}

void loop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  relockDoorIfNeeded();
  updateDoorLight();
  readAndPublishDHT();
  publishToThingSpeak();
}

// WiFi n MQTT stuffs
void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
}

void connectMQTT() {
  espClient.setCACert(root_ca);

  while (!mqttClient.connected()) {
    Serial.print("Connecting to HiveMQ Cloud...");
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("connected!");

      mqttClient.subscribe(TOPIC_PIN_ATTEMPT);
      mqttClient.subscribe(TOPIC_R1_SET);
      mqttClient.subscribe(TOPIC_R2_SET);
      mqttClient.subscribe(TOPIC_R3_SET);
      mqttClient.subscribe(TOPIC_R4_SET);

      mqttClient.publish(TOPIC_DOOR_STATUS, doorUnlocked ? "unlocked" : "locked", true);
      mqttClient.publish(TOPIC_R1_STATE, led2State ? "on" : "off", true);
      mqttClient.publish(TOPIC_R2_STATE, led3State ? "on" : "off", true);
      mqttClient.publish(TOPIC_R3_STATE, led4State ? "on" : "off", true);
      mqttClient.publish(TOPIC_R4_STATE, led5State ? "on" : "off", true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" — retrying in 3s");
      delay(3000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  String t = String(topic);

  Serial.println("[" + t + "] " + msg);

  if (t == TOPIC_PIN_ATTEMPT) {
    handlePinAttempt(msg);
  } else if (t == TOPIC_R1_SET) {
    setRoomLED(LED2_PIN, msg, TOPIC_R1_STATE, led2State);
  } else if (t == TOPIC_R2_SET) {
    setRoomLED(LED3_PIN, msg, TOPIC_R2_STATE, led3State);
  } else if (t == TOPIC_R3_SET) {
    setRoomLED(LED4_PIN, msg, TOPIC_R3_STATE, led4State);
  } else if (t == TOPIC_R4_SET) {
    setRoomLED(LED5_PIN, msg, TOPIC_R4_STATE, led5State);
  }
}

// Door Lock n Pin
void handlePinAttempt(String pin) {
  pin.trim();
  if (pin == DOOR_PIN) {
    unlockDoor();
  } else {
    buzzWrongPin();
    publishAlert("wrong_pin", "Incorrect PIN entered");
    logDoorEventToThingSpeak(false);
  }
}

void unlockDoor() {
  doorServo.write(SERVO_UNLOCKED_ANGLE);
  doorUnlocked = true;
  unlockedAt = millis();
  mqttClient.publish(TOPIC_DOOR_STATUS, "unlocked", true);
  logDoorEventToThingSpeak(true);
}

void relockDoorIfNeeded() {
  if (doorUnlocked && millis() - unlockedAt >= UNLOCK_HOLD_MS) {
    doorServo.write(SERVO_LOCKED_ANGLE);
    doorUnlocked = false;
    mqttClient.publish(TOPIC_DOOR_STATUS, "locked", true);
  }
}

void buzzWrongPin() {
  // 2 Beeps 4 wrong pin
  for (int i = 0; i < 2; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    delay(150);
  }
}

void buzzHighTemp() {
  // 1 long beep for temp warning
  digitalWrite(BUZZER_PIN, HIGH);
  delay(600);
  digitalWrite(BUZZER_PIN, LOW);
}

void publishAlert(const char* type, const char* message) {
  String payload = "{\"type\":\"" + String(type) + "\",\"message\":\"" + String(message) + "\"}";
  mqttClient.publish(TOPIC_ALERTS, payload.c_str());
}

// Door Light PIR LDR
void updateDoorLight() {
  bool motion = digitalRead(PIR_PIN);
  int lightLevel = analogRead(LDR_PIN);
  bool isDark = lightLevel < DARK_THRESHOLD;

  if (isDark) {
    if (motion) {
      lastMotionMillis = millis();
      if (!doorLightOn) {
        doorLightOn = true;
        digitalWrite(LED1_PIN, HIGH);
      }
    }
    if (doorLightOn && millis() - lastMotionMillis > DOORLIGHT_HOLD_MS) {
      doorLightOn = false;
      digitalWrite(LED1_PIN, LOW);
    }
  } else if (doorLightOn) {
    doorLightOn = false;
    digitalWrite(LED1_PIN, LOW);
  }
}

// DHT Thingy
void readAndPublishDHT() {
  if (millis() - lastDHTRead < DHT_INTERVAL_MS) return;
  lastDHTRead = millis();

   float h = dht.readHumidity();
   float t = dht.readTemperature();

   if (isnan(h) || isnan(t)) {
     Serial.println("DHT read failed");
     return;
   }

   lastTemp = t;
   lastHum = h;

  if (isnan(h) || isnan(t)) {
    Serial.println("DHT read failed");
    return;
  }

  String payload = "{\"temp\":" + String(t, 1) + ",\"hum\":" + String(h, 1) + "}";
  mqttClient.publish(TOPIC_DHT, payload.c_str());

  if (t > TEMP_THRESHOLD_C) {
    buzzHighTemp();
    publishAlert("high_temp", "Living room temperature critical");
  }
}

// Room LEDs
void setRoomLED(int pin, String msg, const char* stateTopic, bool &state) {
  msg.trim();
  msg.toLowerCase();
  if (msg == "on") {
    state = true;
    digitalWrite(pin, HIGH);
  } else if (msg == "off") {
    state = false;
    digitalWrite(pin, LOW);
  }
  mqttClient.publish(stateTopic, state ? "on" : "off", true);
}

void publishToThingSpeak() {
  if (millis() - lastThingSpeakPost < THINGSPEAK_INTERVAL_MS) return;
  if (isnan(lastTemp) || isnan(lastHum)) return;
  thingSpeakPostedBefore = true;
  lastThingSpeakPost = millis();

  HTTPClient http;
  String url = "http://api.thingspeak.com/update?api_key=" + String(THINGSPEAK_API_KEY) +
               "&field1=" + String(lastTemp, 1) +
               "&field2=" + String(lastHum, 1);

  http.begin(url);
  int httpCode = http.GET();
  String response = http.getString();

  Serial.print("ThingSpeak HTTP ");
  Serial.print(httpCode);
  Serial.print(" | Entry ID: ");
  Serial.println(response);

  http.end();
}

void logDoorEventToThingSpeak(bool success) {
  Serial.print("DEBUG millis()="); Serial.print(millis());
  Serial.print(" lastDoorEventPost="); Serial.println(lastDoorEventPost);

  if (doorEventPostedBefore && millis() - lastDoorEventPost < THINGSPEAK_DOOR_MIN_INTERVAL_MS) {
    Serial.println("Door event logging skipped (rate limit window)");
    return;
  }
  doorEventPostedBefore = true;
  lastDoorEventPost = millis();

  HTTPClient http;
  String url = "http://api.thingspeak.com/update?api_key=" + String(THINGSPEAK_DOOR_API_KEY) +
               "&field1=" + String(success ? 1 : 0);

  http.begin(url);
  int httpCode = http.GET();
  String response = http.getString();

  Serial.print("Door event ThingSpeak HTTP ");
  Serial.print(httpCode);
  Serial.print(" | Entry ID: ");
  Serial.println(response);

  http.end();
}
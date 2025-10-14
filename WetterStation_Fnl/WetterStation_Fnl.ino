#include "config.h" 
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <RTClib.h>
#include <SD.h>


// ====== PIN DEFINITIONS ======
#define LDR_PIN 34        // Analog input for LDR (light sensor)
#define LED_PIN 32        // Digital output for indicator LED
#define SD_CS   5         // Chip Select for SD card
#define I2C_ADDR 0x27     // I2C address for LCD
#define DHT_TYPE DHT11    // DHT11 sensor type
#define DHT_PIN 17        // DHT11 signal pin
#define FAN_PIN 25        // Fan control pin (connected to transistor base)

// ====== WIFI CONFIGURATION ======
// Siehe datei config.example.h und anpassen :)

// ====== MQTT CONFIGURATION ====== 
// Siehe datei config.example.h und anpassen :)

// ====== OBJECT INITIALIZATION ======
WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHT_PIN, DHT_TYPE);               // Temperature and humidity sensor
LiquidCrystal_I2C lcd(I2C_ADDR, 16, 2);   // LCD display (16x2)
RTC_DS3231 rtc;                           // Real-time clock module

// ====== PARAMETERS ======
int lightLimit = 30;                      // Threshold for light-dependent LED
int fan_limit = 25;                       // Temperature threshold for fan
bool manuel_fan = false;                  // Manual fan override flag

// ====== WIFI CONNECTION ======
void wifiSetup() {
  WiFi.begin(ssid, passwd);
  Serial.println("Attempting Wi-Fi connection...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi connected successfully.");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());
}

// ====== MQTT CALLBACK FUNCTION ======
// Triggered each time a subscribed MQTT topic receives a message
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.println("MQTT received: " + msg);

  // Handle messages for fan control
  if (String(topic) == "home/wetterstation/fan") {
    if (msg == "ON") {
      manuel_fan = true;
      digitalWrite(FAN_PIN, HIGH);
      Serial.println("Fan manually activated!");
    } else if (msg == "OFF") {
      manuel_fan = false;
      digitalWrite(FAN_PIN, LOW);
      Serial.println("Fan manually deactivated!");
    }
  }
}

// ====== MQTT RECONNECTION ======
void reconexion() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT broker...");
    if (client.connect("ESP_32_WetterStation")) {
      Serial.println("connected.");
      client.subscribe("home/wetterstation/fan"); // Subscribe to fan control topic
    } else {
      Serial.print("Failed, status: ");
      Serial.print(client.state());
      Serial.println(". Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\nSystem starting...");

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.print("Wetter Station...");
  delay(1000);
  lcd.clear();

  // Configure I/O
  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);

  // Start sensors and modules
  dht.begin();
  wifiSetup();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Initialize I2C and RTC
  Wire.begin();
  if (!rtc.begin()) {
    Serial.println("Error: RTC not found!");
  } else {
    Serial.println("RTC detected!");
  }

  // Check if RTC has lost power
  delay(500);
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time...");
    delay(500);
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Set to compile time
    Serial.println("Time initialized!");
  } else {
    Serial.println("RTC time already valid.");
  }

  // Initialize SD card
  if (!SD.begin(SD_CS)) {
    Serial.println("Error: SD card not detected!");
  } else {
    Serial.println("SD card ready.");
  }
}

// ====== MAIN LOOP ======
void loop() {
  if (!client.connected()) {
    reconexion();
  }
  client.loop();

  // Read sensor data
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  int lightVal = analogRead(LDR_PIN);
  DateTime aktuel_time = rtc.now();

  // Validate DHT reading
  if (isnan(temp) || isnan(hum)) {
    Serial.println("Error reading DHT11!");
    return;
  }

  // Control LED based on light intensity
  digitalWrite(LED_PIN, lightVal < lightLimit ? HIGH : LOW);

  // Display sensor data on LCD
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(temp);
  lcd.print((char)223);
  lcd.print("C H:");
  lcd.print(hum);
  lcd.print("%");

  lcd.setCursor(0, 1);
  lcd.print("Light:");
  lcd.print(lightVal);
  lcd.print(" ");

  // Automatic fan control (if not in manual mode)
  if (!manuel_fan) {
    if (temp > fan_limit) {
      digitalWrite(FAN_PIN, HIGH);
      Serial.println("High temp → fan ON");
    } else {
      digitalWrite(FAN_PIN, LOW);
    }
  }

  // Log data to SD card
  File file = SD.open("/log.txt", FILE_APPEND);
  if (file) {
    file.printf("%02d/%02d/%04d %02d:%02d:%02d | T=%.2f°C H=%.2f%% L=%d\n",
                aktuel_time.day(), aktuel_time.month(), aktuel_time.year(),
                aktuel_time.hour(), aktuel_time.minute(), aktuel_time.second(),
                temp, hum, lightVal);
    file.close();
  }

  // Publish data to MQTT broker
  String payload = "{";
  payload += "\"temp\":" + String(temp, 1) + ",";
  payload += "\"hum\":" + String(hum, 1) + ",";
  payload += "\"light\":" + String(lightVal) + ",";
  payload += "\"timestamp\":\"" + String(aktuel_time.timestamp()) + "\"";
  payload += "}";

  client.publish("home/wetterstation", payload.c_str());
  Serial.println("MQTT -> " + payload);

  delay(5000);
}

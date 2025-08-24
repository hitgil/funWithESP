#include "DHTesp.h"
#include <EEPROM.h>

#define PIR_PIN 25
#define PMETER_PIN 35
#define DHT22_PIN 26
#define LED_PIN 2
#define LED_PIN_SPI 5
#define PIN_BTN 13
#define PIN_BTN_TWO 32

#define EEPROM_SIZE 4      
#define ADD_COUNT  0      

static int var_one;
static int var_two;
DHTesp dht;

unsigned long lastSendTime = 0;
unsigned long lastMotionTime = 0;
unsigned long lastHeartbeatTime = 0;
unsigned long lastBtnPress = 0;
unsigned int count = 0;

bool pirTriggered = false; 

enum SystemState {
  ACTIVE,
  STANDBY,
  LOW_POWER
};

SystemState currentState = ACTIVE;

void IRAM_ATTR handleInterrupt(void) {
  unsigned long currentTime = millis();
  if (currentTime - lastBtnPress > 200) {
    if (digitalRead(PIN_BTN) == LOW) {
      if (currentState == ACTIVE) {
        currentState = STANDBY;
      } else if (currentState == STANDBY) {
        currentState = ACTIVE;
      }
    }
    lastBtnPress = currentTime;
  }
}

void IRAM_ATTR pirInterrupt(void) {
  pirTriggered = true;
  lastMotionTime = millis();
}

void setup() {
  Serial.begin(115200);
  dht.setup(DHT22_PIN, DHTesp::DHT22);
  
  pinMode(PIR_PIN, INPUT);
  pinMode(PMETER_PIN, INPUT); 
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_BTN_TWO, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_PIN_SPI, OUTPUT);

  attachInterrupt(digitalPinToInterrupt(PIN_BTN), handleInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirInterrupt, RISING);
  lastMotionTime = millis(); 

  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("Failed to initialise EEPROM!");
    while (1);
  }

  EEPROM.get(ADD_COUNT, count);
  Serial.printf("System Boot → Previous Low Power Entries: %u\n", count);
}

void handleActiveMode() {
  if (millis() - lastHeartbeatTime >= 2000) {
    lastHeartbeatTime = millis();
    Serial.println("{\"spi_heartbeat\": \"OK\"}");
  }

  var_one = digitalRead(PIR_PIN);
  var_two = analogRead(PMETER_PIN);
  TempAndHumidity data = dht.getTempAndHumidity();

  if (var_one == HIGH) {
    digitalWrite(LED_PIN, HIGH);
    lastMotionTime = millis();  
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  if (millis() - lastSendTime >= 5000) {
    lastSendTime = millis();
    String jsonData = "{";
    jsonData += "\"pir\":" + String(var_one) + ",";
    jsonData += "\"pot\":" + String(var_two) + ",";
    jsonData += "\"temperature\":" + String(data.temperature, 2) + ",";
    jsonData += "\"humidity\":" + String(data.humidity, 2) + ",";
    jsonData += "}";
    Serial.println(jsonData);
  }

  if (digitalRead(PIN_BTN_TWO) == LOW) {  
    digitalWrite(LED_PIN_SPI, !digitalRead(LED_PIN_SPI));
    Serial.println("Mock SPI Data Received -> Toggled SPI LED");
    delay(300); 
  }

  if (millis() - lastMotionTime >= 30000) {
    count++;
    EEPROM.put(ADD_COUNT, count);
    EEPROM.commit();
    currentState = LOW_POWER;
    Serial.printf("Entering LOW POWER MODE... Count: %u\n", count);
  }
}

void loop() {
  switch (currentState) {
    case ACTIVE:
      handleActiveMode();
      break;

    case STANDBY:
      Serial.println("STAND BY MODE");
      delay(500);
      break;

    case LOW_POWER:
      delay(500); 
      if (pirTriggered) {
        pirTriggered = false;
        currentState = ACTIVE;
        Serial.println("Motion detected → Wake up!");
      }
      break;
  }
}

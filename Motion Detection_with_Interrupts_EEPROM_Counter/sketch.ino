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

unsigned int lastSendTime = 0;
unsigned int lastMotionTime = 0;
unsigned int lastHeartbeatTime = 0;
unsigned long lastBtnPress = 0;
unsigned int count = 0;

bool standByMode = false;
bool lowPowerMode = false;
bool pirTriggered = false; 


void IRAM_ATTR handleInterrupt(void){
  unsigned long currentTime = millis();
  if (currentTime - lastBtnPress > 10) {  // 10ms debounce
    if (digitalRead(PIN_BTN) == LOW) {
      standByMode = true;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    } else {
      standByMode = false;
    }
    lastBtnPress = currentTime;
  }
}

void IRAM_ATTR pirInterrupt(void){
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

  attachInterrupt(digitalPinToInterrupt(PIN_BTN), handleInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirInterrupt, RISING);
  lastMotionTime = millis(); 

  // EEPROM init
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("Failed to initialise EEPROM!");
    while (1);
  }

  // Load previous counter
  EEPROM.get(ADD_COUNT, count);
  Serial.printf("System Boot → Previous Low Power Entries: %u\n", count);
}


void loop() {
  if (standByMode) {
    Serial.println("STAND BY MODE");
    delay(500);
    return;
  }

  if (pirTriggered) {
    pirTriggered = false;               
    lowPowerMode = false;               
    Serial.println("Motion detected → Wake up!");
  }

  if (!lowPowerMode && (millis() - lastMotionTime >= 30000)) {
    count++;
    EEPROM.put(ADD_COUNT, count);
    EEPROM.commit();
    lowPowerMode = true;
    Serial.printf("Entering LOW POWER MODE... Count: %u\n", count);
  }

  if (lowPowerMode) {
    delay(500); 
    return;
  }

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
    jsonData += "\"lowPowerCount\":" + String(count);
    jsonData += "}";
    Serial.println(jsonData);
  }

  if (digitalRead(PIN_BTN_TWO) == LOW) {  
    digitalWrite(LED_PIN_SPI, !digitalRead(LED_PIN_SPI));
    delay(300); 
    Serial.println("Mock SPI Data Received -> Toggled SPI LED");
  }
}

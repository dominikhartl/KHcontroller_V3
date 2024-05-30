#include <Arduino.h>
#include <FS.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h> // Include EEPROM library
#include <ArduinoJson.h> // Include ArduinoJson library
#include <credentials.h>

const char* mqtt_server = "homeserver.local";
int port = 1883;
char DevName[] = "KHcontrollerV3";




float voltage_4PH; //= 1812.0;
float voltage_7PH; // = 1292.0;

int titrationVolume = 2;
int TitrationSteps = 16;
int titrationSpeed = 400;
int titrationDelay = 500;
int titrationFirst = 3000;
int drops = 0;
int titrationMeasure = 6000;
int dropsMax = 12000;
int fillVolume = 100;
int preFillSpeed = 300;
int SampleVolume = 350;
float spd = 400;
float slope = 0.9995;
float bottomph = 4.3;
int stirrerSpeed = 230;
int nreadings = 25;
int mesDelay = 50;

const uint16_t OTA_CHECK_INTERVAL = 3000; // ms
uint32_t _lastOTACheck = 0;

#define ESPADC 4096.0
#define ESPVOLTAGE 3300
const int stepsPerRevolution = 1600;

#define EN_PIN1 25
#define DIR_PIN1 32
#define STEP_PIN1 2
#define EN_PIN2 22
#define DIR_PIN2 27
#define STEP_PIN2 4
#define Stirrer 16
#define PH_PIN 35
#define MQTT_MAX_PACKET_SIZE 512
#define MQTT_KEEPALIVE 30

char output[50];
char MQmsg[50];
char MQerr[50];
char MQKH[50];
char MQpH[50];
char MQstartpH[50];
char MQmespH[50];
char MQlog[50];

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

float voltage, pH, StartpH, H;

void StartStirrer() {
  Serial.println("starting stirrer");
  analogWrite(Stirrer, stirrerSpeed);
}

void StopStirrer() {
  analogWrite(Stirrer, 0);
}

void(* resetFunc) (void) = 0;

void RemoveSample(int SampleVolume) {
  digitalWrite(EN_PIN1, LOW);
  digitalWrite(DIR_PIN1, LOW);
  float acc = 2000.0;
  while (acc > spd) {
    digitalWrite(STEP_PIN1, HIGH);
    delayMicroseconds(acc);
    digitalWrite(STEP_PIN1, LOW);
    delayMicroseconds(acc);
    acc = acc * slope;
  }
  for (uint16_t e = SampleVolume; e > 0; e--) {
    for (uint16_t i = stepsPerRevolution; i > 0; i--) {
      digitalWrite(STEP_PIN1, HIGH);
      delayMicroseconds(spd);
      digitalWrite(STEP_PIN1, LOW);
      delayMicroseconds(spd);
    }
  }
  digitalWrite(EN_PIN1, HIGH);
}

void TakeSample(int SampleVolume) {
  digitalWrite(EN_PIN1, LOW);
  digitalWrite(DIR_PIN1, HIGH);
  float acc = 2000.0;
  while (acc > spd) {
    digitalWrite(STEP_PIN1, HIGH);
    delayMicroseconds(acc);
    digitalWrite(STEP_PIN1, LOW);
    delayMicroseconds(acc);
    acc = acc * slope;
  }
  for (uint16_t e = SampleVolume; e > 0; e--) {
    for (uint16_t i = stepsPerRevolution; i > 0; i--) {
      digitalWrite(STEP_PIN1, HIGH);
      delayMicroseconds(spd);
      digitalWrite(STEP_PIN1, LOW);
      delayMicroseconds(spd);
    }
  }
  digitalWrite(EN_PIN1, HIGH);
}

void Wash(float remPart, float fillPart) {
  RemoveSample(SampleVolume * remPart);
  TakeSample(SampleVolume * fillPart);
}

void Titrate(int Volume, int tspd) {
  digitalWrite(EN_PIN2, LOW);
  digitalWrite(DIR_PIN2, LOW);
  for (uint16_t i = Volume; i > 0; i--) {
    for (uint16_t e = TitrationSteps; e > 0; e--) {
      digitalWrite(STEP_PIN2, HIGH);
      delayMicroseconds(tspd);
      digitalWrite(STEP_PIN2, LOW);
      delayMicroseconds(tspd);
    }
  }
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  // Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  Serial.println("Connecting to MQTT Broker...");
  while (!client.connected()) {
    Serial.println("Reconnecting to MQTT Broker..");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("Connected.");
      client.subscribe(output);
    }
  }
}

void saveVoltage4PH(float voltage) {
  EEPROM.begin(512);
  EEPROM.put(0, voltage);
  EEPROM.commit();
}

float retrieveVoltage4PH() {
  float voltage;
  EEPROM.begin(512);
  EEPROM.get(0, voltage);
  return voltage;
}

void saveVoltage7PH(float voltage) {
  EEPROM.begin(512);
  EEPROM.put(sizeof(float), voltage);
  EEPROM.commit();
}

float retrieveVoltage7PH() {
  float voltage;
  EEPROM.begin(512);
  EEPROM.get(sizeof(float), voltage);
  return voltage;
}

void measurePH(int nreadings) {
  float sum = 0.0;
  voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
  for (int t = 0; t < nreadings; t++) {
    voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
    H = (voltage - voltage_7PH) / (voltage_4PH - voltage_7PH) * 4.0 + (voltage - voltage_4PH) / (voltage_7PH - voltage_4PH) * 7.0;
    sum += H;
    delay(mesDelay);
  }
  pH = sum / nreadings;
}

float measureVoltage(int nreadings) {
  float V = 0.0;
  float sum = 0.0;
  voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
  for (int t = 0; t < nreadings; t++) {
    voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
    sum += voltage;
    delay(mesDelay);
  }
  V = sum / nreadings;
  return V;
}

void MeasureKH() {
  drops = 0;
  int lowcnt = 0;
  client.publish(MQerr, "All good so far!");
  Titrate(fillVolume, preFillSpeed);
  digitalWrite(EN_PIN2, HIGH);
  Wash(1.2, 1.0);
  delay(100);
  StartStirrer();
  client.publish(MQmsg, "Reading start pH");
  measurePH(100);
  StartpH = pH;
  if (isnan(pH)) {
    Serial.println("Error: pH probe not working");
    client.publish(MQerr, "Error: pH probe not working");
  }
  Serial.print("StartpH:");
  Serial.println(StartpH, 2);
  if (StartpH < 6) {
    Serial.println("Error: Starting pH too low");
    client.publish(MQerr, "Error: Starting pH too low");
  } else {
    if (!client.connected()) {
      delay(1000);
      reconnect();
    }
    client.loop();
    
    // First drops fast
    Titrate(titrationFirst, titrationSpeed);
    drops = titrationFirst;
    measurePH(50);
    while ((pH > (bottomph-0.1)) & (!isnan(pH)) & (drops < dropsMax) & (lowcnt < 5)) {
      Titrate(titrationVolume, titrationSpeed);
      if (!client.connected()) {
        delay(1000);
        reconnect();
      }
      if (pH > (bottomph +1)) {
        delay(titrationDelay);
        measurePH(1);
      } else if (pH > (bottomph + 0.5)) {
        delay(titrationDelay / 2);
        measurePH(5);
      } else if (pH > (bottomph + 0.2)) {
        delay(titrationDelay);
        measurePH(10);
      } else if (pH > bottomph) {
        delay(titrationDelay * 5);
        measurePH(50);
      }else if (pH < bottomph) {
        lowcnt++;
        delay(titrationDelay * 5);
        measurePH(100);
      }
      drops = drops + titrationVolume;
      
      Serial.println(pH);
      client.publish(MQmespH, String(pH).c_str());
      if (isnan(pH)) {
        Serial.println("Error: pH probe not working");
        client.publish(MQerr, "Error: pH probe not working!");
        StopStirrer();
        digitalWrite(EN_PIN2, HIGH);
        break;
      }
      if (drops == (dropsMax - 1)) {
        Serial.println("Error: reached acid max!");
        client.publish(MQerr, "Error: reached acid max!");
        StopStirrer();
        digitalWrite(EN_PIN2, HIGH);
        break;
      }
    }
    if (!client.connected()) {
      delay(1000);
      reconnect();
    }
    client.loop();
    Serial.print("drops:");
    Serial.println(drops);
    client.publish(MQmsg, String("Drops: " + String(drops)).c_str());
    client.publish(MQKH, String(drops).c_str());
    client.publish(MQstartpH, String(StartpH).c_str());
    digitalWrite(EN_PIN2, HIGH);
  }
  drops = 0;
  StopStirrer();
  Wash(1.5, 1);
}

void sendConfig() {
  StaticJsonDocument<1024> doc;

  doc["voltage_4PH"] = voltage_4PH;
  doc["voltage_7PH"] = voltage_7PH;
  doc["drops"] = drops;
  doc["StartpH"] = StartpH;
  doc["titrationVolume"] = titrationVolume;
  doc["TitrationSteps"] = TitrationSteps;
  doc["titrationSpeed"] = titrationSpeed;
  doc["titrationDelay"] = titrationDelay;
  doc["titrationMeasure"] = titrationMeasure;
  doc["dropsMax"] = dropsMax;
  doc["fillVolume"] = fillVolume;
  doc["preFillSpeed"] = preFillSpeed;
  doc["SampleVolume"] = SampleVolume;
  doc["spd"] = spd;
  doc["slope"] = slope;
  doc["stirrerSpeed"] = stirrerSpeed;
  doc["nreadings"] = nreadings;
  doc["mesDelay"] = mesDelay;
  doc["stepsPerRevolution"] = stepsPerRevolution;

  char jsonBuffer[1024];
  serializeJson(doc, jsonBuffer);

  client.publish(MQlog, jsonBuffer);
}


void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageM;

  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageM += (char)message[i];
  }
  Serial.println();

  if (String(topic) == output) {
    if (messageM == "k") {
      Serial.println("measuring KH!");
      if (client.publish(MQmsg, "Measuring KH!") == true) {
        Serial.println("Message Succeeded!");
      }
      MeasureKH();
      sendConfig();
    } else if (messageM == "p") {
      measurePH(100);
      if (isnan(pH)) {
        Serial.println("Error: pH probe not working");
        client.publish(MQerr, "Error: pH probe not working");
      }
      Serial.print("pH:");
      Serial.println(pH, 2);
      client.publish(MQpH, String(pH).c_str());
    } else if (messageM == "f") {
      Titrate(fillVolume, preFillSpeed);
      Serial.println("Filling");
      digitalWrite(EN_PIN2, HIGH);
    } else if (messageM == "s") {
      Serial.println("measuring sample volume");
      client.publish(MQmsg, "measuring sample volume");
      Wash(1.2, 1.0);
      Serial.println("done");
      client.publish(MQmsg, "done");
    } else if (messageM == "t") {
      Serial.println("calibrating titration pump");
      drops = 0;
      Titrate(titrationFirst, titrationSpeed);
      drops = titrationFirst;
      while ((drops < titrationMeasure)) {
        Titrate(titrationVolume, titrationSpeed);
        delay(titrationDelay);
        client.publish(MQmsg, String(drops).c_str());
        drops = drops + titrationVolume;
      }
      drops = 0;
      digitalWrite(EN_PIN2, HIGH);
      Serial.println("done");
      client.publish(MQmsg, "done");
    } else if (messageM == "m") {
      StartStirrer();
    } else if (messageM == "e") {
      digitalWrite(Stirrer, LOW);
      Serial.println("stopping stirrer");
    } else if (messageM == "r") {
      Serial.println("removing sample");
      RemoveSample(SampleVolume);
      Serial.println("done");
    } else if (messageM == "o") {
      Serial.println("resetting!");
      client.publish(MQmsg, "resetting");
      resetFunc();
    } else if (messageM == "v") {
      client.publish(MQmsg, String(measureVoltage(100)).c_str());
    } else if (messageM == "4") {
      voltage_4PH = measureVoltage(100);
      saveVoltage4PH(voltage_4PH);
      Serial.print("Saved voltage_4PH to EEPROM: ");
      Serial.println(voltage_4PH);
      client.publish(MQmsg, "Calibrated pH 4");
    } else if (messageM == "7") {
      voltage_7PH = measureVoltage(100);
      saveVoltage7PH(voltage_7PH);
      Serial.print("Saved voltage_7PH to EEPROM: ");
      Serial.println(voltage_7PH);
      client.publish(MQmsg, "Calibrated pH 7");
    }
  }
}

void setupMQTT() {
  client.setServer(mqtt_server, port);
  client.setCallback(callback);
}

void setup() {
  pinMode(EN_PIN1, OUTPUT);
  pinMode(STEP_PIN1, OUTPUT);
  pinMode(DIR_PIN1, OUTPUT);
  pinMode(EN_PIN2, OUTPUT);
  pinMode(STEP_PIN2, OUTPUT);
  pinMode(DIR_PIN2, OUTPUT);
  pinMode(Stirrer, OUTPUT);
  pinMode(PH_PIN, INPUT);
  Serial.begin(9600);

  snprintf(output, sizeof(output), "%s/output", DevName);
  snprintf(MQmsg, sizeof(MQmsg), "%s/message", DevName);
  snprintf(MQerr, sizeof(MQerr), "%s/error", DevName);
  snprintf(MQKH, sizeof(MQKH), "%s/KH", DevName);
  snprintf(MQpH, sizeof(MQpH), "%s/pH", DevName);
  snprintf(MQstartpH, sizeof(MQstartpH), "%s/startPH", DevName);
  snprintf(MQmespH, sizeof(MQmespH), "%s/mes_pH", DevName);
  snprintf(MQlog, sizeof(MQlog), "%s/log", DevName); // Initialize MQlog
  
  setup_wifi();
  setupMQTT();

  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname(DevName);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  voltage_4PH = retrieveVoltage4PH();
  voltage_7PH = retrieveVoltage7PH();
  Serial.print("Retrieved voltage_4PH from EEPROM: ");
  Serial.println(voltage_4PH);
  Serial.print("Retrieved voltage_7PH from EEPROM: ");
  Serial.println(voltage_7PH);
}

void loop() {
  ArduinoOTA.handle();
  if (!client.connected()) {
    delay(5000);
    reconnect();
  }
  client.loop();

  long now = millis();
  if (now - lastMsg > 5000) {
    lastMsg = now;
  }

  while (Serial.available() > 0) {
    delay(100);
    char command = Serial.read();
    switch (command) {
      case 'v':
        measureVoltage(100);
        break;
      case 'p':
        measurePH(100);
        Serial.println(pH);
        break;
      case 's':
        StartStirrer();
        break;
      case '4':
        voltage_4PH = measureVoltage(100);
        saveVoltage4PH(voltage_4PH);
        Serial.print("Saved voltage_4PH to EEPROM: ");
        Serial.println(voltage_4PH);
        break;
      case '7':
        voltage_7PH = measureVoltage(100);
        saveVoltage7PH(voltage_7PH);
        Serial.print("Saved voltage_7PH to EEPROM: ");
        Serial.println(voltage_7PH);
        break;
      case '1':
        voltage_4PH = retrieveVoltage4PH();
        Serial.println(voltage_4PH);
        break;
      case '2':
        voltage_7PH = retrieveVoltage7PH();
        Serial.println(voltage_7PH);
        break;
    }
  }
}
#include <Arduino.h>
#include <FS.h>
#include <mDNS.h>
#include <ArduinoOTA.h>
/*
   Measure KH: k
   measure pH: p
   fill titration tube: f
   measure sample volume: s
   measure titration volume: t
   start stirrer: m
   end stirrer: e
   remove sample: r
   Calibration:
     c enterph -> enter the calibration mode
     c calph   -> calibrate with the standard buffer solution, two buffer solutions(4.0 and 7.0) will be automaticlly recognized
     c exitph  -> save the calibrated parameters and exit from calibration mode

*/

// Replace the next variables with your SSID/Password combination
const char* ssid = ""; //Add SSID of your WIFI here
const char* password = ""; //Add WIFI Password here
//if you have several devices you have to adjust the name
char DevName[] = "KHcontrollerV3";
char output[] ="KHcontrollerV3/output";

// Add your MQTT Broker IP address:
const char* mqtt_server = ""; //add your mqtt server IP
int        port     = 1883;

// general Firmware settings, only adjust if you know what you are doing
int titrationVolume = 1;
int TitrationSteps = 16;
int SampleVolume = 300;
int count = 0;
int titrationSpeed = 150;
int titrationDelay = 500;
int drops = 0;
int titrationMeasure = 7000;
int dropsMax = 12000;
int fillVolume = 100;
int preFillSpeed = 500;
int spd = 400;
int stirrerSpeed = 230;
int nreadings = 25;
int mesDelay = 50;
int FirstSpeed = 500;
int FastDrops = 5000;

const uint16_t OTA_CHECK_INTERVAL = 3000; // ms
uint32_t _lastOTACheck = 0;

#include <TMCStepper.h>
#include <DFRobot_ESP_PH.h>
#include <EEPROM.h>

DFRobot_ESP_PH ph;
#define ESPADC 4096.0   //the esp Analog Digital Convertion value
#define ESPVOLTAGE 3300 //the esp voltage supply value
const int stepsPerRevolution = 1600;

// definition of IO pins, do not change if you use the standard PCB
#define EN_PIN1           25 // Enable
#define DIR_PIN1          32 // Direction
#define STEP_PIN1         2 // Step
#define EN_PIN2           22 // Enable
#define DIR_PIN2          27 // Direction
#define STEP_PIN2         4 // Step
#define Stirrer            16
#define PH_PIN             35
#define MQTT_MAX_PACKET_SIZE 512
#define MQTT_KEEPALIVE 30

#include <WiFi.h>
#include <PubSubClient.h>

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;


float voltage, pH, StartpH, H, temperature = 25;

void StartStirrer() {
  Serial.println("starting stirrer");
  analogWrite(Stirrer, stirrerSpeed);
}

void StopStirrer(){
  analogWrite(Stirrer, 0);
}

void(* resetFunc) (void) = 0;

void RemoveSample(int SampleVolume){
  digitalWrite(EN_PIN1, LOW);
  digitalWrite(DIR_PIN1, LOW); 
  for (uint16_t e = SampleVolume; e>0; e--) {
    for (uint16_t i = stepsPerRevolution; i>0; i--) {
      digitalWrite(STEP_PIN1, HIGH);
      delayMicroseconds(spd);
      digitalWrite(STEP_PIN1, LOW);
      delayMicroseconds(spd);
    }
  }
  digitalWrite(EN_PIN1, HIGH);  
}

void TakeSample(int SampleVolume){
  digitalWrite(EN_PIN1, LOW);
  digitalWrite(DIR_PIN1, HIGH); 
  for (uint16_t e = SampleVolume; e>0; e--) {
    for (uint16_t i = stepsPerRevolution; i>0; i--) {
      digitalWrite(STEP_PIN1, HIGH);
      delayMicroseconds(spd);
      digitalWrite(STEP_PIN1, LOW);
      delayMicroseconds(spd);
    }
  }
  digitalWrite(EN_PIN1, HIGH);
}

void Wash(float remPart,float fillPart){
  RemoveSample(SampleVolume*remPart);
  TakeSample(SampleVolume * fillPart);
}

void Titrate(int Volume, int spd){
  digitalWrite(EN_PIN2, LOW); 
  digitalWrite(DIR_PIN2, LOW);
  for (uint16_t i = Volume; i>0; i--) {
    for (uint16_t e=TitrationSteps; e>0; e--){
    digitalWrite(STEP_PIN2, HIGH);
    delayMicroseconds(spd);
    digitalWrite(STEP_PIN2, LOW);
    delayMicroseconds(spd);
  }
}
}


void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

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
        // subscribe to topic
        client.subscribe(output);
      }
      
  }
}

void measurePH(int nreadings){
  float sum = 0.0 ;
  voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
  for (int t = 0; t < nreadings; t++) {
    voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
    H = ph.readPH(voltage, temperature);
    sum += H ;
    delay(mesDelay);
  }
  pH =  sum / nreadings;
}

void MeasureKH() {
  // Titration program
  /// Fill HCL tube
  Titrate(fillVolume, preFillSpeed);
  digitalWrite(EN_PIN2, LOW);
  // Wash
  Wash(1.0,1.0);
  
  // Titration
  /// start stirrer
  delay(100);
  StartStirrer();
  //Measure starting pH and send serial
  client.publish("KHcontrollerV3/message", "Reading start pH");
  measurePH(100);
  StartpH = pH;
  if(isnan(pH)){
    Serial.println("Error: pH probe not working");
    client.publish("KHcontrollerV3/error", "Error: pH probe not working");
  }
  Serial.print("StartpH:");
  Serial.println(StartpH, 2);
  if (StartpH < 6) {
    Serial.println("Error: Starting pH too low");
    client.publish("KHcontrollerV3/error", "Error: Starting pH too low");
  }
  else {
    ///start titration
    client.publish("KHcontrollerV3/message", "First drops fast!");
    Titrate(FastDrops, FirstSpeed);
    drops = FastDrops;
    delay(5000);
    
    if (!client.connected()) {
      delay(1000);
      reconnect();
    }
    client.loop();
    measurePH(100);
    while ((pH > 4.5) & (!isnan(pH)) & (drops < dropsMax)) {
      Titrate(titrationVolume,titrationSpeed);
      if (pH>5){
      delay(titrationDelay);
      measurePH(2);
      }else if(pH>4.7){
      delay(titrationDelay*2);
      measurePH(25);
      }
      else if(pH>4.5){
      delay(titrationDelay*10);
      measurePH(100);
      }
      drops = drops + 1;
      
      Serial.println(pH);
      client.publish("KHcontrollerV3/mes_pH",String(pH).c_str());
        if(isnan(pH)){
          Serial.println("Error: pH probe not working");
          client.publish("KHcontrollerV3/error", "Error: pH probe not working!");
          StopStirrer();
          digitalWrite(EN_PIN2, HIGH);
          break;
        }
      // client.publish(DevName, String(pH).c_str());
      if (drops == (dropsMax - 1)) {
        Serial.println("Error: reached acid max!");
        client.publish("KHcontrollerV3/error", "Error: reached acid max!");
        StopStirrer();
        digitalWrite(EN_PIN2, HIGH);
        break;
      }
    }
    if (!client.connected()) {
      delay(5000);
      reconnect();
    }
    client.loop();
    Serial.print("drops:");
    Serial.println(drops);
    client.publish("KHcontrollerV3/message", String("Drops: " + String(drops)).c_str());
    client.publish("KHcontrollerV3/KH",String(drops).c_str());
    client.publish("KHcontrollerV3/startPH",String(StartpH).c_str());
    
    digitalWrite(EN_PIN2, HIGH);
  }
  drops = 0;
  StopStirrer();
  Wash(1.5,0.8);
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

    // If a message is received on the topic KHcontrollerV2/output, you check if the message is either "on" or "off". 
    // Changes the output state according to the message
    if (String(topic) == output) {
      if(messageM == "k"){
          Serial.println("measuring KH!");
          if(client.publish("KHcontrollerV3/message", "Measuring KH!") == true){
            Serial.println("Message Succeeded!");          
          }
          MeasureKH();
      } else if(messageM == "p"){
          voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
          pH = ph.readPH(voltage, temperature);
            if(isnan(pH)){
              Serial.println("Error: pH probe not working");
              client.publish("KHcontrollerV3/error", "Error: pH probe not working");
            }
          Serial.print("pH:");
          Serial.println(pH, 2);
          client.publish("KHcontrollerV3/pH",String(pH).c_str());
      }

        else if(messageM == "c"){
          voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
          ph.calibration(voltage, temperature);
        }

        else if(messageM == "f"){
          Titrate(fillVolume, preFillSpeed);
          Serial.println("Filling");
          digitalWrite(EN_PIN2, HIGH);
        }

        else if(messageM == "s"){
          Serial.println("measuring sample volume");
          client.publish("KHcontrollerV3/message", "measuring sample volume");
          RemoveSample(SampleVolume);
          delay(1000);
          TakeSample(SampleVolume);
          Serial.println("done");
          client.publish("KHcontrollerV3/message", "done");
        }

        else if(messageM == "t"){
          Serial.println("calibrating titration pump");
          Titrate(FastDrops, FirstSpeed);
          drops = FastDrops;
          while (drops < (titrationMeasure)) {
            Titrate(titrationVolume,titrationSpeed);
            drops = drops + 1;
            Serial.println(drops);
            client.publish("KHcontrollerV3/message", String(drops).c_str());
            delay(titrationDelay);
          }
          drops=0;
          digitalWrite(EN_PIN2, HIGH);
          Serial.println("done");
          client.publish("KHcontrollerV3/message", "done");
        }

        else if(messageM == "m"){
          StartStirrer();
        }

        else if(messageM == "e"){
          digitalWrite(Stirrer, LOW);
          Serial.println("stopping stirrer");
        }

        else if(messageM == "r"){
          Serial.println("removing sample");
          RemoveSample(SampleVolume);
          Serial.println("done");
        }

        else if(messageM == "o"){
          Serial.println("resetting!");
          client.publish("KHcontrollerV3/message", "resetting");
          resetFunc();
        }
        else if(messageM == "enterph"){
          std::string str = "ENTERPH";
          char *cstr = new char[str.length() + 1];
          strcpy(cstr, str.c_str());
          voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
          Serial.println(voltage);
          ph.calibration(voltage, temperature,cstr);
        }
        else if(messageM == "calph"){
          std::string str = "CALPH";
          char *cstr = new char[str.length() + 1];
          strcpy(cstr, str.c_str());
          voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
          Serial.println(voltage);
          ph.calibration(voltage, temperature,cstr);
        }
        else if(messageM == "exitph"){
          std::string str = "EXITPH";
          char *cstr = new char[str.length() + 1];
          strcpy(cstr, str.c_str());
          voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
          Serial.println(voltage);
          ph.calibration(voltage, temperature,cstr);
        }
      }
}



void setupMQTT() {
  client.setServer(mqtt_server , port);
  // set the callback function
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
  setup_wifi();
  setupMQTT();
  EEPROM.begin(32);
	ph.begin();

  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname("khcontroller");

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

  while (Serial.available() > 0 ) {
  delay(100);
  char command = Serial.read();
  switch (command) {
      case 'c':
      voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
      Serial.println(voltage);
      ph.calibration(voltage, temperature);
      break;
  case 'p':
      measurePH(100);
      Serial.println(pH);
      break;
  case 's':
    StartStirrer();
    break;
  case 'a':
          std::string str = "ENTERPH";
          char *cstr = new char[str.length() + 1];
          strcpy(cstr, str.c_str());
          voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGE;
          Serial.println(voltage);
          ph.calibration(voltage, temperature,cstr);
  }
  }
}
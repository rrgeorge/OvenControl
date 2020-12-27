#include <ArduinoJson.h>

#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SPI.h>

//WiFi Settings
const char* ssid = ""; // WiFi SSID
const char* password = ""; // WiFi password
const int port = 80; //  Receiving HTTP port
const char* hostName = "oven";

#define MAX6675_CS   D7
#define MAX6675_SO   D6
#define MAX6675_SCK  D8

#define ROTARY_CLK D3
#define ROTARY_DT D4
#define ROTARY_SW D5

// Only read temperature ever 10 secs
const int window = 10000;
//Adjust temperature by 45°
const int adj = -40;

int rotation;
int value;
boolean LeftRight;
int RotPosition = 0;
int swState = 1;

struct Oven {
  boolean on = false;
  boolean heating = false;
  boolean preheating = false;
  double target = 0;
  double current = 0;
  unsigned long lastread = 0;
} oven;

uint8_t DegreeBitmap[]= { 0x6, 0x9, 0x9, 0x6, 0x0, 0, 0, 0 };
byte flameChar[] = { B01000, B01100, B01110, B01110, B11011, B11011, B10001, B01110 };

LiquidCrystal_I2C lcd(0x27, 16, 2);

ESP8266WebServer server(port);

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  Serial.println("ESP8266 Oven Controller");
  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Hello!");
  lcd.setCursor(0,1);
  lcd.print("Connecting");
  WiFi.hostname(hostName);
  WiFi.begin(ssid, password);
  WiFi.softAPdisconnect(true);  
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      lcd.print(".");
  }
  lcd.setCursor(0,1);
  lcd.print("                ");
  lcd.setCursor(0,1);
  lcd.print("Connected!");
  lcd.createChar ( 1, DegreeBitmap );
  lcd.createChar ( 2, flameChar );
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());
  pinMode (ROTARY_CLK,INPUT);
  pinMode (ROTARY_DT,INPUT);
  pinMode (ROTARY_SW,INPUT);
  pinMode (D0,OUTPUT);
  digitalWrite(D0, LOW);
  delay(1000);
  lcd.noBacklight();
  server.on("/", []() {
    server.send(200,"text/plain","Server is active");
  });
  server.on("/status", []() {
    char json[200];
    DynamicJsonDocument doc(192);
    oven.current = readThermocoupleF() + adj;
    doc["on"] = oven.on;
    doc["heating"] = oven.heating;
    doc["preheating"] = oven.preheating;
    doc["target"] = oven.target;
    doc["current"] = oven.current;
    doc["lastread"] = oven.lastread;
    serializeJson(doc, json);
    server.send(200,"application/json",json);
  });
 
  server.on("/set", []() {
    char json[200];
    DynamicJsonDocument doc(192);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      doc["error"] = err.code();
      serializeJson(doc, json);
      server.send(500,"application/json",json);
    } else {
      oven.on = doc["on"];
      oven.target = doc["target"];

      if (oven.on) {
        lcd.clear();
        lcd.backlight();
        lcd.setCursor(0,0);
        lcd.print("Remotely activated");
        delay(500);
        oven.current = readThermocoupleF() + adj;
        lcd.setCursor(0,0);
        lcd.print("                ");
        lcd.setCursor(0,0);
        lcd.print("Preheat to:");
        lcd.print((int)round(oven.target));
        lcd.print("\001F");
        lcd.setCursor(0,1);
        lcd.print("                ");
        lcd.setCursor(0,1);
        lcd.print("Current:   ");
        lcd.print((int)round(oven.current));
        lcd.print("\001F");
        oven.lastread = 0;
        oven.preheating = true;
      } else {
        digitalWrite(D0, LOW); 
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Oven is off");
        oven.heating = false;
        delay(500);
        lcd.noBacklight();
      }

      doc.clear();
      
      doc["on"] = oven.on;
      doc["heating"] = oven.heating;
      doc["preheating"] = oven.preheating;
      doc["target"] = oven.target;
      doc["current"] = oven.current;
      doc["lastread"] = oven.lastread;
      
      serializeJson(doc, json);
      server.send(200,"application/json",json);
    }
  });
  server.begin();
}

void loop() {
  server.handleClient();
  // put your main code here, to run repeatedly:
  unsigned long now = millis();
  if (oven.on) {
    if (now - oven.lastread > window) {
      oven.lastread = now;
      double currentTemp = readThermocoupleF() + adj;
      if (currentTemp < oven.target - 15) {
        if (!oven.heating) {
          lcd.setCursor(15,0);
          lcd.print("\002");
          digitalWrite(D0, HIGH);
        }
        oven.heating = true;
      } else if (currentTemp > oven.target - 5)  {
        if (oven.heating) {
          lcd.setCursor(15,0);
          lcd.print(" ");
          oven.heating = false;
          digitalWrite(D0, LOW);
        }
      }
      if (currentTemp != oven.current) {
        oven.current = currentTemp;
        lcd.setCursor(0,1);
        lcd.print("                ");
        lcd.setCursor(0,1);
        lcd.print("Current:   ");

        lcd.print((int)round(currentTemp));
        lcd.print("\001F");
        if (oven.preheating && oven.current >= oven.target ) {
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("Preheated to");
          lcd.setCursor(2,1);
          lcd.print((int)round(oven.target));
          lcd.print("\001F");
          for (int i = 0; i < 3; i ++) {
            lcd.noBacklight();
            delay(1000);
            lcd.backlight();
            delay(1000);
          }
          lcd.setCursor(0,0);
          lcd.setCursor(0,0);
          lcd.print("                ");
          lcd.setCursor(0,0);
          lcd.print("Preheated: ");
          lcd.print((int)round(oven.target));
          lcd.print("\001F");
          oven.preheating = false;
        }
      }
    }
  }

  value = digitalRead(ROTARY_CLK);
  if (value != rotation){ // we use the DT pin to find out which way we turning.
     if (digitalRead(ROTARY_DT) != value) {  // Clockwise
       RotPosition ++;
       LeftRight = true;
       if (oven.on && oven.target < 500 && (RotPosition % 2) == 0) {
          oven.target += 5;
          oven.preheating = true;
          lcd.setCursor(0,0);
          lcd.print("                ");
          lcd.setCursor(0,0);
          lcd.print("Preheat to:");
          lcd.print((int)round(oven.target));
          lcd.print("\001F");
       }
     } else { //Counterclockwise
       LeftRight = false;
       RotPosition--;
       if (oven.on && oven.target > 10 && (RotPosition % 2) == 0 ) {
          oven.target -= 5;
          oven.preheating = true;
          lcd.setCursor(0,0);
          lcd.print("                ");
          lcd.setCursor(0,0);
          lcd.print("Preheat to:");
          lcd.print((int)round(oven.target));
          lcd.print("\001F");
       }
     }
     if (oven.heating) {
        lcd.setCursor(15,0);
        lcd.print("\002");
      } else {
        lcd.setCursor(15,0);
        lcd.print(" ");
      }
   }
   rotation = value;
   int swR = digitalRead(ROTARY_SW);
   if (swR != swState) {
       swState = swR;
       if (swState == 1) {
         Serial.println(swState);
         if (oven.on) {
          digitalWrite(D0, LOW); 
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("Oven is off");
          oven.on = false;
          oven.heating = false;
          delay(500);
          lcd.noBacklight();
         } else {
          lcd.clear();
          lcd.backlight();
          lcd.setCursor(0,0);
          lcd.print("Oven is on");
          oven.on = true;
          oven.target = 200;
          delay(500);
          oven.current = readThermocoupleF() + adj;
          lcd.setCursor(0,0);
          lcd.print("                ");
          lcd.setCursor(0,0);
          lcd.print("Preheat to:");
          lcd.print((int)round(oven.target));
          lcd.print("\001F");
          lcd.setCursor(0,1);
          lcd.print("                ");
          lcd.setCursor(0,1);
          lcd.print("Current:   ");
          lcd.print((int)round(oven.current));
          lcd.print("\001F");
          oven.lastread = 0;
          oven.preheating = true;
         }
       }
   }
}

double readThermocoupleF() {
  return readThermocouple()*1.8+32;
}

double readThermocouple() {
  uint16_t v;
  pinMode(MAX6675_CS, OUTPUT);
  pinMode(MAX6675_SO, INPUT);
  pinMode(MAX6675_SCK, OUTPUT);
  
  digitalWrite(MAX6675_CS, LOW);
  delay(1);

  // Read in 16 bits,
  //  15    = 0 always
  //  14..2 = 0.25 degree counts MSB First
  //  2     = 1 if thermocouple is open circuit  
  //  1..0  = uninteresting status
  
  v = shiftIn(MAX6675_SO, MAX6675_SCK, MSBFIRST);
  v <<= 8;
  v |= shiftIn(MAX6675_SO, MAX6675_SCK, MSBFIRST);
  
  digitalWrite(MAX6675_CS, HIGH);
  if (v & 0x4) 
  {    
    // Bit 2 indicates if the thermocouple is disconnected
    return NAN;     
  }

  // The lower three bits (0,1,2) are discarded status bits
  v >>= 3;

  // The remaining bits are the number of 0.25 degree (C) counts
  return v*0.25;
}

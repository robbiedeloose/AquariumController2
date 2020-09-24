#include <Arduino.h>
#include "Credentials.h"

// WIFI, MQTT & OTA
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#define HOSTNAME "aquarium110-2"

#define VERSION 0.3

// If not using the Credentials.h file you can add credentials here
#ifndef STASSID 
#define STASSID "ssid"
#define STAPSK  "passkey"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;
//const char* mqtt_server = "broker.mqtt-dashboard.com";
IPAddress mqtt_server(192, 168, 10, 161);
boolean noWifiMode = false;
boolean mqttServerConnected = false;

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];
int value = 0;

// PINS
#define PIN_PUMP1 14
#define PIN_RELAY_CO2 13
#define PIN_RELAY_AIR 15
bool co2;
bool air;


// RTC
#include <Wire.h> // must be included here so that Arduino library object file references work
#include <RtcDS3231.h>
RtcDS3231<TwoWire> Rtc(Wire);
int sunriseHour = 9;
int sunriseMinute = 0;
int sunsetHour = 21;
int sunsetMinute = 0;
int moonStartHour = 22;
int moonStartMinute = 0;
int moonStopHour = 2;
int moonStopMinute = 0;
int airStartHour = 21;
int airStartMinute = 0;
int airStopHour = 2;
int airStopMinute = 0;
int duration = 15; // in minutes
int waitRGB = duration * 60 * 1000 / 255 / 2;
int waitWhite = duration * 60 * 1000 / 1024 / 2;

int moonBrightness = 150;
int moonNumberofleds = 4;

boolean daylight = false;
boolean EEPRomOverwrite = false;

int pump1Dosage = 10; // 10 ml check and calculate running time
int pump1RunDays[7] = {1,0,0,0,0,0,0}; // mo, tue, wen, thu, fri, sa, su

// Millis
int period = 5000;
unsigned long time_now = 0;

// EEPROM
#include <EEPROM.h>

// Display
#include <Adafruit_I2CDevice.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// TIME FUNCTIONS ///////////////////////////////////////////////////////////////////////////////////////////////

#define countof(a) (sizeof(a) / sizeof(a[0]))

void printDateTime(const RtcDateTime& dt)
{
    char datestring[20];

    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
            dt.Month(),
            dt.Day(),
            dt.Year(),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
    Serial.print(datestring);
    Serial.println();
    Serial.println(dt.DayOfWeek());

}

boolean checkTime(const RtcDateTime& dt,int setHour, int setMinute){
  int hourNow = dt.Hour();
  int minuteNow = dt.Minute();  
  if (hourNow == setHour) {
    if (minuteNow == setMinute) {
      return true;
    }
    else return false;
  }
  else return false;
}

void writeValuesToEEPRom (){
  		EEPROM.write(0, 1);
			EEPROM.write(1, sunriseHour);
			EEPROM.write(2, sunriseMinute);
			EEPROM.write(3, sunsetHour);
			EEPROM.write(4, sunsetMinute);
			EEPROM.write(5, duration);
      EEPROM.write(6, moonStartHour);
      EEPROM.write(7, moonStartMinute);
      EEPROM.write(8, moonStopHour);
      EEPROM.write(9, moonStopMinute);
      EEPROM.write(10, airStartHour);
      EEPROM.write(11, airStartMinute);
      EEPROM.write(12, airStopHour);
      EEPROM.write(13, airStopMinute);
			EEPROM.commit();
			EEPROM.end();
}

void readValuesFromEEPRom(){
  			sunriseHour = EEPROM.read(1);
				sunriseMinute = EEPROM.read(2);
				sunsetHour = EEPROM.read(3);
				sunsetMinute = EEPROM.read(4);
				duration = EEPROM.read(5);
        moonStartHour = EEPROM.read(6);
        moonStartMinute = EEPROM.read(7);
        moonStopHour = EEPROM.read(8);
        moonStopMinute = EEPROM.read(9);
        airStartHour = EEPROM.read(10);
        airStartMinute = EEPROM.read(11);
        airStopHour = EEPROM.read(12);
        airStopMinute = EEPROM.read(13);
}

//EEPROM FUNCTIONS /////////////////////////////////////////////////////////////////////////////////////////////
void startEEPRom () {
	  EEPROM.begin(512);
		int EEPRomIsSet = EEPROM.read(0);
    if ( EEPRomIsSet == 1 ) {
      Serial.println("Alarm time is allready set in EEPROM. Found the following data:");
      readValuesFromEEPRom();
    } else {
      Serial.println("No Alarm time is EEPROM, saving default alarm times. Using following data:");
      writeValuesToEEPRom();
    }
    waitRGB = duration * 60 * 1000 / 255 / 2;
    waitWhite = duration * 60 * 1000 / 1024 / 2;
    Serial.printf ("LIGHT: start %02d:%02d, stop: %02d:%02d, Duration: %d minutes\n", sunriseHour, sunriseMinute, sunsetHour , sunsetMinute, duration);
    Serial.printf ("MOON:  start %02d:%02d, stop: %02d:%02d, Duration: %d minutes\n", moonStartHour, moonStartMinute, moonStopHour , moonStopMinute, duration);
}

//RGB FUNCTIONS /////////////////////////////////////////////////////////////////////////////////////////////



// WIFI FUNCTIONS ///////////////////////////////////////////////////////////////////////////////

void reconnect() {
	int mqttTimeOut = 0;
  while (!client.connected()) {
    if (mqttTimeOut < 5) {
      Serial.print("Attempting MQTT connection...");
      // Create a random client ID 
      String clientId = "ESP8266Client-";
      clientId += String(random(0xffff), HEX);
      // Attempt to connect
      if (client.connect(clientId.c_str())) {
        Serial.println("connected");
        Serial.println("send wakeupmassege");
        client.publish("homie/aquarium110", "reconnected");
        // ... and resubscribe
        Serial.println("subscribe");
        client.subscribe("homie/aquarium110/#");
        mqttServerConnected = true;
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 1 seconds");
        delay(1000);
      }
      mqttTimeOut++;
    }
    else{
      Serial.println("Not connected to MQTT");
      mqttServerConnected = false;
      return;
    }
  }
  Serial.println("MQTT reconnect done");
	mqttServerConnected = true;
}

void setup_wifi() {

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

	int wifiTimeOut = 0;
  while (WiFi.status() != WL_CONNECTED) {
		if (wifiTimeOut < 60){
			delay(500);
			Serial.print(".");
			noWifiMode = false;
			wifiTimeOut++;
		}
		else {
			noWifiMode = true;
			return;
		}
  }

  randomSeed(micros());

	if (noWifiMode == false) {
		Serial.println("");
  	Serial.println("WiFi connected");
  	Serial.print("IP address: ");
  	Serial.println(WiFi.localIP());
	}
	else {
		Serial.println("");
		Serial.print("Could not connect to ");
		Serial.println(ssid);
		Serial.println("Started without wifi");
	}

}



// MQTT FUNCTIONS ///////////////////////////////////////////////////////////////////////////////

void mqttSendInfo () {
  char tempBuffer[100] = "";
    client.publish("homie/aquarium110/info", "Aquarium 110L, bottom controller");
    sprintf(tempBuffer, "IP: %s\n", WiFi.localIP().toString().c_str());
    client.publish("homie/aquarium110/info", tempBuffer);
    sprintf(tempBuffer, "Suntrise: %02d:%02d\n", sunriseHour, sunriseMinute);
    client.publish("homie/aquarium110/info", tempBuffer);
    sprintf(tempBuffer, "Suntset: %02d:%02d\n", sunsetHour, sunsetMinute);
    client.publish("homie/aquarium110/info", tempBuffer);
    sprintf(tempBuffer, "duration: %02d\n", duration);
    client.publish("homie/aquarium110/info", tempBuffer);
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  char buf[20] = "";
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    buf[i] = (char)payload[i];
  }
  buf[length] = '\0';
  Serial.println();

  // Overwrite main light over mqtt
  if(strcmp(topic, "homie/aquarium110/light") == 0){
    int payloadAsInt = atoi ((char*)payload);
    analogWrite(12, map(payloadAsInt, 0, 100, 0, 1023 ));
    Serial.printf ("Main light at %d percent", payloadAsInt);
  } 
 
  // set new sunrise time over mqtt
  else if (strcmp(topic, "homie/aquarium110/sunrise") == 0) {
    char * strtokIndx; // this is used by strtok() as an index

    strtokIndx = strtok((char*)payload,":");
    sunriseHour = atoi(strtokIndx); 

    strtokIndx = strtok(NULL, ":");
    sunriseMinute = atoi(strtokIndx);     

    EEPROM.begin(512);
    EEPROM.write(1, sunriseHour);
		EEPROM.write(2, sunriseMinute);
		EEPROM.commit();
		EEPROM.end();

    Serial.printf ("New sunrise time: %02d:%02d", sunriseHour, sunriseMinute);
		Serial.println();  
  }
  // set new sunset time over mqtt
  else if (strcmp(topic, "homie/aquarium110/sunset") == 0) {
    char * strtokIndx; 

    strtokIndx = strtok((char*)payload,":");  
    sunsetHour = atoi(strtokIndx); 
    strtokIndx = strtok(NULL, ":");
    sunsetMinute = atoi(strtokIndx);     

    EEPROM.begin(512);
    EEPROM.write(3, sunsetHour);
		EEPROM.write(4, sunsetMinute);
		EEPROM.commit();
		EEPROM.end();

    Serial.printf ("New sunset time: %02d:%02d", sunriseHour, sunriseMinute);
		Serial.println();  
  }
  // Set new sunrise and sunset duration
  else if (strcmp(topic, "homie/aquarium110/duration") == 0) {
    duration = atoi ((char*)payload);

    EEPROM.begin(512);
    EEPROM.write(5, duration);
		EEPROM.commit();
		EEPROM.end();
    Serial.printf ("New duration time: %d", duration);
		Serial.println();  
  } 
  // Request Ip from this controller
  else if(strcmp(topic, "homie/aquarium110/requestip") == 0){
    char tempBuffer[20] = "";
    sprintf(tempBuffer, "IP: %s\n", WiFi.localIP().toString().c_str());
    client.publish("homie/aquarium110/ip", tempBuffer);
  }
  // Request Ip from this controller
  else if(strcmp(topic, "homie/aquarium110/requestinfo") == 0){
    mqttSendInfo();
  }
  else if (strcmp(topic, "homie/aquarium110/co2") == 0){
    int payloadAsInt = atoi ((char*)payload);
    if (payloadAsInt == 1) {
      co2 = true;
      digitalWrite(PIN_RELAY_CO2, HIGH);
    }
    else {
      co2 = false;
      digitalWrite(PIN_RELAY_CO2, LOW);      
    }
  }
   else if (strcmp(topic, "homie/aquarium110/air") == 0){
    int payloadAsInt = atoi ((char*)payload);
    if (payloadAsInt == 1) {
      air = true;
      digitalWrite(PIN_RELAY_AIR, HIGH);
    }
    else {
      air = false;
      digitalWrite(PIN_RELAY_AIR, LOW);      
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);
  while (!Serial); // wait for serial port to connect. Needed for native USB on ESP8266
  delay(5000);
  Serial.println("Serial started");
  delay(1000);

  // WIFI
  setup_wifi();

	if (noWifiMode == false) {
		client.setServer(mqtt_server, 1883);
		client.setCallback(callback);

    // OTA 
		// Hostname defaults to esp8266-[ChipID]
		#ifdef HOSTNAME 
		ArduinoOTA.setHostname(HOSTNAME);
		#endif

		ArduinoOTA.onStart([]() {
			String type;
			if (ArduinoOTA.getCommand() == U_FLASH) {
				type = "sketch";
			} else { // U_FS
				type = "filesystem";
			}

			// NOTE: if updating FS this would be the place to unmount FS using FS.end()
			Serial.println("Start updating " + type);
		});
		ArduinoOTA.onEnd([]() {
			Serial.println("\nEnd");
		});
		ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
			Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
		});
		ArduinoOTA.onError([](ota_error_t error) {
			Serial.printf("Error[%u]: ", error);
			if (error == OTA_AUTH_ERROR) {
				Serial.println("Auth Failed");
			} else if (error == OTA_BEGIN_ERROR) {
				Serial.println("Begin Failed");
			} else if (error == OTA_CONNECT_ERROR) {
				Serial.println("Connect Failed");
			} else if (error == OTA_RECEIVE_ERROR) {
				Serial.println("Receive Failed");
			} else if (error == OTA_END_ERROR) {
				Serial.println("End Failed");
			}
		});
		ArduinoOTA.begin();
		Serial.println("Ready");
		Serial.print("IP address: ");
	}	

  //display ip address
  Serial.println(WiFi.localIP());

  pinMode(PIN_RELAY_CO2, OUTPUT);
  pinMode(PIN_RELAY_AIR, OUTPUT);

  // Setup RTC //
  Serial.print("compiled: ");
  Serial.print(__DATE__);
  Serial.println(__TIME__);

  Rtc.Begin();

  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  printDateTime(compiled);

  if (!Rtc.IsDateTimeValid()) 
  {
      if (Rtc.LastError() != 0)
      {
        Serial.print("RTC communications error = ");
        Serial.println(Rtc.LastError());
      }
      else
      {   
        Serial.println("RTC lost confidence in the DateTime!");
        Rtc.SetDateTime(compiled);
      }
      delay(2000);
  }

  if (!Rtc.GetIsRunning())
  {
    Serial.println("RTC was not actively running, starting now");
    Rtc.SetIsRunning(true);
    delay(2000);
  }

  RtcDateTime now = Rtc.GetDateTime();
  if (now < compiled) 
  {
    Serial.println("RTC is older than compile time!  (Updating DateTime)");
    delay(2000);
    Rtc.SetDateTime(compiled);
  }
  else if (now > compiled) 
  {
    Serial.println("RTC is newer than compile time. (this is expected)");
    delay(2000);
  }
  else if (now == compiled) 
  {
    Serial.println("RTC is the same as compile time! (not expected but all is fine)");
  }

  // never assume the Rtc was last configured by you, so
  // just clear them to your needed state
  Rtc.Enable32kHzPin(false);
  Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone); 

	startEEPRom();

  ////////////////////////
  // check if light should be on at startup
	/*
	int minutesSinceMidnight = now.Hour() * 60 + now.Minute();
	int minutesOn = sunriseHour * 60 + sunriseMinute;
	int minutesOff = sunsetHour * 60 + sunsetMinute;

	if (minutesSinceMidnight > minutesOn) {
		 daylight = true;
		 if (minutesSinceMidnight > minutesOff) {
			 daylight = false;
		 }
	}
	else {
		daylight = false;
	}	
	if (daylight) {
		sunrise();
  	setRGB1();
	}
*/

  //setRGB2();
}

void loop() {
  
	if (noWifiMode == false) {
		if (!client.connected()) {
			reconnect();
		}
		client.loop();
		ArduinoOTA.handle();
	}

  if (millis() > time_now + period) {
		time_now = millis();

		if (!Rtc.IsDateTimeValid()) 
    {
        if (Rtc.LastError() != 0)
        {
            // we have a communications error
            // see https://www.arduino.cc/en/Reference/WireEndTransmission for 
            // what the number means
            Serial.print("RTC communications error = ");
            Serial.println(Rtc.LastError());
        }
        else
        {
            // Common Causes:
            //    1) the battery on the device is low or even missing and the power line was disconnected
            Serial.println("RTC lost confidence in the DateTime!");
        }
    }

  RtcDateTime now = Rtc.GetDateTime();
  printDateTime(now);
  Serial.println();

	RtcTemperature temp = Rtc.GetTemperature();
	temp.Print(Serial);
	// you may also get the temperature as a float and print it
    // Serial.print(temp.AsFloatDegC());
    Serial.println("C");

    // display stuff

  //// Check the time every x seconds and perform an action 
	//// Check sunrise and sunset 
    if (checkTime(now, sunriseHour, sunriseMinute)) {
      daylight = true;
    }
    if (checkTime(now, sunsetHour, sunsetMinute)) {
      daylight = false;
    }
    Serial.print("Daylight: ");
    Serial.println(daylight);
    
    //// check CO2
    if (checkTime(now, sunriseHour - 2, sunriseMinute)) {
      co2 = true;
      digitalWrite(PIN_RELAY_CO2, HIGH);
    }
    if (checkTime(now, sunsetHour - 2, sunsetMinute)) {
      co2 = false;
      digitalWrite(PIN_RELAY_CO2, LOW);
    }

    //// check AIR
    if (checkTime(now, airStartHour, sunriseMinute)) {
      air = true;
      digitalWrite(PIN_RELAY_AIR, HIGH);
    }
    if (checkTime(now, sunsetHour, sunsetMinute)) {
      air = false;
      digitalWrite(PIN_RELAY_AIR, LOW);
    }
  }
}
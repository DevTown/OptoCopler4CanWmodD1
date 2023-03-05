#include <WiFiManager.h> 
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>


#define VERSION "0.0.1"

#ifndef TOPICROOT
#define TOPICROOT "OptoCoppler2MQTT"
#endif

#ifndef DEBUGPRINT
#define DEBUGPRINT  0
#endif
#define LogTarget Serial

#include "config.h"
#include "wmconfig.h"   

WiFiClient espClient;
PubSubClient client(espClient);

bool first_connect = true;


void reconnect() {
  while (!client.connected()) {
    LogTarget.print("Attempting MQTT connection...");
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), sett.mqtt_user, sett.mqtt_password, (((String)TOPICROOT "/" + devname + "/status").c_str()), 1, true, "offline")) {
      LogTarget.println("connected");
      client.publish((((String)TOPICROOT "/" + devname + "/status").c_str()), "online", true);
      client.subscribe(TOPICROOT "/#");
      LogTarget.println("MQTT ok");
    } else {
      LogTarget.print("failed, rc=");
      LogTarget.print(client.state());
      LogTarget.println(" try again in 5 seconds");
      LogTarget.println("MQTT error");
      delay(5000);
    }
  }
}


void setup() {
  Serial.begin(115200);
  
  delay(500);
  Serial.println();
  Serial.println();
  Serial.println("OptoCoppler2MQTT Version " VERSION);
  Serial.println();
  setup_wifi();
  
  client.setServer(sett.mqtt_broker, 1883);
  client.setCallback(mqttReceiveCallback);
  client.setBufferSize(MAX_TEXT_LENGTH);

  pinMode(PIN_ON, OUTPUT);
  pinMode(PIN_OFF, OUTPUT);
  pinMode(PIN_LIGHT, OUTPUT);
  pinMode(PIN_TIMER, OUTPUT);
}

void loop() {
  ArduinoOTA.handle();

  if (!client.connected()) {
    reconnect();
    LogTarget.println("ready");
    if (do_publishes)
      if (first_connect) {
        client.publish((((String)TOPICROOT "/" + devname + "/status").c_str()), "startup");
        client.publish((((String)TOPICROOT "/" + devname + "/status").c_str()), ((String)"version " + VERSION).c_str());
        client.publish((((String)TOPICROOT "/" + devname + "/status").c_str()), ((String)"ip " + ip.toString()).c_str());
        first_connect = false;
      }
    client.publish((((String)TOPICROOT "/" + devname + "/status").c_str()), "reconnect");
  }
  client.loop();

}



void mqttReceiveCallback(char* topic, byte* payload, unsigned int length)
{

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  
  const bool pr = DEBUGPRINT;                   // set to 1 for debug prints

  if (String(topic).endsWith("status")) return; // don't process stuff we just published

  String command = topic + String(topic).lastIndexOf(TOPICROOT "/") + strlen(TOPICROOT) + 1;
  command.toLowerCase();
  
  Serial.print("Command: ");
  Serial.print(command);
  Serial.println();

  
  if (command.equals(Topic_On)|| command.equals(Topic_Off)||command.equals(Topic_Light)||command.equals(Topic_Timer)) {
     char msg = (char)payload[0];
     
     if(msg == '1')
     {
       Serial.print("Make it on!");
       if(command.equals(Topic_On))
       {
          Serial.print("High");
          digitalWrite(PIN_ON, HIGH); // sets the digital pin 13 on
          delay(1000);            // waits for a second
          digitalWrite(PIN_ON, LOW); 
        }
        if( command.equals(Topic_Off))
        {
          Serial.print("Low");
          digitalWrite(PIN_OFF, HIGH); // sets the digital pin 13 on
          delay(1000);            // waits for a second
          digitalWrite(PIN_OFF, LOW); 
        }
        if(command.equals(Topic_Light))
        {
          Serial.print("Light");
          digitalWrite(PIN_LIGHT, HIGH); // sets the digital pin 13 on
          delay(1000);            // waits for a second
          digitalWrite(PIN_LIGHT, LOW); 
        }
        if(command.equals(Topic_Timer))
        {
          Serial.print("Timer");
          digitalWrite(PIN_TIMER, HIGH); // sets the digital pin 13 on
          delay(1000);            // waits for a second
          digitalWrite(PIN_TIMER, LOW); 
       }
      }
      Serial.println();
  }
}

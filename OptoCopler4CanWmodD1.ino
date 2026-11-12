#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>


#define VERSION "0.1.0"

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
bool reconnect_pending = true;
unsigned long lastReconnectAttempt = 0;
unsigned long wifiLostSince = 0;

String topicStatus;   // online / offline (retained, LWT)  -> availability
String topicInfo;     // startup, version, ip, reconnect, rssi
String topicAck;      // last executed button press         -> diagnosis


// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------
struct Button {
  const char* topic;
  uint8_t pin;
};

const Button buttons[] = {
  { Topic_On,    PIN_ON    },
  { Topic_Off,   PIN_OFF   },
  { Topic_Light, PIN_LIGHT },
  { Topic_Timer, PIN_TIMER },
};
const uint8_t NUM_BUTTONS = sizeof(buttons) / sizeof(buttons[0]);

#define QUEUE_SIZE 8
uint8_t pressQueue[QUEUE_SIZE];
uint8_t queueHead = 0;
uint8_t queueCount = 0;

enum PressState { PS_IDLE, PS_WAKE_PRESS, PS_WAKE_GAP, PS_PRESS, PS_GAP };
PressState pressState = PS_IDLE;
uint8_t activeButton = 0;
bool activeWithWake = false;
unsigned long stateSince = 0;
unsigned long lastPressEnd = 0;
bool everPressed = false;


void initPins() {
  // Drive all outputs LOW as early as possible so the optocouplers are not
  // floating (= random key presses) while WiFi / WiFiManager start up.
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    digitalWrite(buttons[i].pin, LOW);
    pinMode(buttons[i].pin, OUTPUT);
    digitalWrite(buttons[i].pin, LOW);
  }
}

bool enqueuePress(uint8_t idx) {
  if (queueCount >= QUEUE_SIZE) return false;
  pressQueue[(queueHead + queueCount) % QUEUE_SIZE] = idx;
  queueCount++;
  return true;
}

bool remoteProbablyAsleep(unsigned long now) {
#if WAKEUP_PULSE_ENABLED
  return !everPressed || (now - lastPressEnd) > REMOTE_IDLE_TIMEOUT_MS;
#else
  (void)now;
  return false;
#endif
}

void publishAck() {
  if (!do_publishes || !client.connected()) return;
  String msg = buttons[activeButton].topic;
  if (activeWithWake) msg += " (wake)";
  client.publish(topicAck.c_str(), msg.c_str());
}

// Non-blocking button state machine - MQTT, WiFi and OTA keep running while a
// button is "held".
void handleButtons() {
  unsigned long now = millis();
  uint8_t pin = buttons[activeButton].pin;

  switch (pressState) {
    case PS_IDLE:
      if (queueCount == 0) return;
      activeButton = pressQueue[queueHead];
      queueHead = (queueHead + 1) % QUEUE_SIZE;
      queueCount--;
      pin = buttons[activeButton].pin;
      activeWithWake = remoteProbablyAsleep(now);
      LogTarget.print("Press ");
      LogTarget.print(buttons[activeButton].topic);
      LogTarget.println(activeWithWake ? " (with wake pulse)" : "");
      digitalWrite(pin, HIGH);
      pressState = activeWithWake ? PS_WAKE_PRESS : PS_PRESS;
      stateSince = now;
      break;

    case PS_WAKE_PRESS:
      if (now - stateSince >= WAKE_PULSE_MS) {
        digitalWrite(pin, LOW);
        pressState = PS_WAKE_GAP;
        stateSince = now;
      }
      break;

    case PS_WAKE_GAP:
      if (now - stateSince >= WAKE_GAP_MS) {
        digitalWrite(pin, HIGH);
        pressState = PS_PRESS;
        stateSince = now;
      }
      break;

    case PS_PRESS:
      if (now - stateSince >= PRESS_MS) {
        digitalWrite(pin, LOW);
        lastPressEnd = now;
        everPressed = true;
        publishAck();
        pressState = PS_GAP;
        stateSince = now;
      }
      break;

    case PS_GAP:
      if (now - stateSince >= RELEASE_GAP_MS) {
        pressState = PS_IDLE;
      }
      break;
  }
}


// ---------------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------------
bool mqttConnect() {
  LogTarget.print("Attempting MQTT connection...");

  // stable client id: a random id leaves "ghost" sessions on the broker and
  // makes broker logs hard to read
  const char* user = strlen(sett.mqtt_user) ? sett.mqtt_user : NULL;
  const char* pass = strlen(sett.mqtt_password) ? sett.mqtt_password : NULL;

  if (client.connect(devname, user, pass, topicStatus.c_str(), 1, true, "offline")) {
    LogTarget.println("connected");
    client.publish(topicStatus.c_str(), "online", true);
    // only the command level (TOPICROOT/<cmd>), not our own status/info topics
    client.subscribe(TOPICROOT "/+", 1);
    return true;
  }

  LogTarget.print("failed, rc=");
  LogTarget.print(client.state());
  LogTarget.println(" try again later");
  return false;
}

void handleMqtt() {
  if (client.connected()) {
    client.loop();
    return;
  }

  unsigned long now = millis();
  if (!reconnect_pending && now - lastReconnectAttempt < MQTT_RECONNECT_MS) return;
  lastReconnectAttempt = now;
  reconnect_pending = false;

  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqttConnect()) return;

  LogTarget.println("ready");
  if (do_publishes) {
    if (first_connect) {
      client.publish(topicInfo.c_str(), "startup");
      client.publish(topicInfo.c_str(), ((String)"version " + VERSION).c_str());
      first_connect = false;
    } else {
      client.publish(topicInfo.c_str(), "reconnect");
    }
    client.publish(topicInfo.c_str(), ((String)"ip " + WiFi.localIP().toString()).c_str());
    client.publish(topicInfo.c_str(), ((String)"rssi " + WiFi.RSSI()).c_str());
  }
}

void handleWifiWatchdog() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiLostSince = 0;
    return;
  }
  unsigned long now = millis();
  if (wifiLostSince == 0) {
    wifiLostSince = now ? now : 1;
    LogTarget.println("WiFi lost");
  } else if (now - wifiLostSince > WIFI_LOST_RESTART_MS) {
    LogTarget.println("WiFi lost for too long, restarting");
    ESP.restart();
  }
}

bool isPressPayload(byte* payload, unsigned int length) {
  if (length == 0 || length > 8) return false;   // empty = retained delete etc.
  char buf[9];
  for (unsigned int i = 0; i < length; i++) buf[i] = tolower((char)payload[i]);
  buf[length] = '\0';
  String msg(buf);
  msg.trim();
  return msg == "1" || msg == "on" || msg == "true" || msg == "press";
}

void mqttReceiveCallback(char* topic, byte* payload, unsigned int length)
{
  String t(topic);
  String command = t.substring(t.lastIndexOf('/') + 1);
  command.toLowerCase();

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (!isPressPayload(payload, length)) return;

  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    if (command.equals(buttons[i].topic)) {
      if (!enqueuePress(i)) LogTarget.println("press queue full, command dropped");
      return;
    }
  }
}


// ---------------------------------------------------------------------------
void setup() {
  initPins();                 // first thing: no floating optocoupler inputs

  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println();
  Serial.println("OptoCoppler2MQTT Version " VERSION);
  Serial.println();

  setup_wifi();

  topicStatus = (String)TOPICROOT "/" + devname + "/status";
  topicInfo   = (String)TOPICROOT "/" + devname + "/info";
  topicAck    = (String)TOPICROOT "/" + devname + "/last_command";

  client.setServer(sett.mqtt_broker, MQTT_PORT);
  client.setCallback(mqttReceiveCallback);
  client.setBufferSize(MAX_TEXT_LENGTH);
  client.setKeepAlive(MQTT_KEEPALIVE_S);
  client.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
}

void loop() {
  ArduinoOTA.handle();
  handleWifiWatchdog();
  handleMqtt();
  handleButtons();
}

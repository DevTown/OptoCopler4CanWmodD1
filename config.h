#pragma once

// set to false if you do not want to publish anything:
const bool do_publishes = true;

#define MAX_TEXT_LENGTH 1500

// ---------------------------------------------------------------------------
// MQTT command topics:  <TOPICROOT>/<Topic_xxx>   payload "1" (or "on"/"true"/"press")
// e.g.  OptoCoppler2MQTT/on  ->  1
// IMPORTANT: publish commands WITHOUT the retain flag, otherwise every
// reconnect of the ESP replays the last command (= unwanted button press).
// ---------------------------------------------------------------------------
#define Topic_On     "on"
#define Topic_Off    "off"
#define Topic_Light  "light"
#define Topic_Timer  "timer"

// ---------------------------------------------------------------------------
// Pins (Wemos D1 mini)
// NOTE: D3 = GPIO0 is a boot strapping pin with a 10k pull-up on the board.
// It is HIGH during boot before the sketch runs (-> "OFF" may be pressed on
// every reset) and a load to GND can prevent booting. If you rewire, prefer
// D1 (GPIO5) or D7 (GPIO13) instead of D3/D4/D8.
// ---------------------------------------------------------------------------
#define PIN_ON     D2
#define PIN_OFF    D3
#define PIN_LIGHT  D5
#define PIN_TIMER  D6

// ---------------------------------------------------------------------------
// Button timing
// ---------------------------------------------------------------------------
// How long a "finger" is held on the remote button. 1000 ms was the original
// value. Many remotes interpret very long presses as "hold"/repeat; 200..400 ms
// is usually closer to a human key press - try it if presses get lost.
#define PRESS_MS          1000
// Minimum pause between two queued button presses (remote must see a release).
#define RELEASE_GAP_MS    400

// ---------------------------------------------------------------------------
// Optional "wake-up" press
// Some battery remotes sleep and use the first key press only to wake up
// (backlight / MCU start) without sending anything. If the physical remote
// shows that behaviour, enable this: if no button was pressed for
// REMOTE_IDLE_TIMEOUT_MS, a short wake press is sent before the real press.
// ---------------------------------------------------------------------------
#define WAKEUP_PULSE_ENABLED     0
#define WAKE_PULSE_MS            150
#define WAKE_GAP_MS              500
#define REMOTE_IDLE_TIMEOUT_MS   10000UL

// ---------------------------------------------------------------------------
// Network robustness
// ---------------------------------------------------------------------------
#define MQTT_PORT                1883
#define MQTT_KEEPALIVE_S         10      // detect dead connections faster (lib default 15)
#define MQTT_SOCKET_TIMEOUT_S    5
#define MQTT_RECONNECT_MS        5000UL
// Config portal (opened when WiFi cannot connect) closes after this time and
// the ESP restarts - otherwise a router outage at boot blocks the device forever.
#define WIFI_PORTAL_TIMEOUT_S    180
// Restart if WiFi stays disconnected for this long.
#define WIFI_LOST_RESTART_MS     300000UL

# Dunstabzug – Falmec Aura per MQTT (OptoCopler4CanWmodD1)

ESP8266-Firmware (Wemos D1 mini), die die **Original-Fernbedienung einer Falmec Aura
Dunstabzugshaube** über Optokoppler „drückt“. Gesteuert wird per MQTT, z. B. aus
Home Assistant.

Aktuelle Version: **0.1.0** (siehe [Changelog](#changelog))

---

## Inhalt

1. [Funktionsprinzip](#funktionsprinzip)
2. [Hardware](#hardware)
3. [Dateien im Repository](#dateien-im-repository)
4. [MQTT-Schnittstelle](#mqtt-schnittstelle)
5. [Konfiguration (`config.h`)](#konfiguration-configh)
6. [Ersteinrichtung (WLAN / MQTT-Broker)](#ersteinrichtung-wlan--mqtt-broker)
7. [Bauen & Flashen](#bauen--flashen)
8. [Prüfen, ob die Firmware läuft](#prüfen-ob-die-firmware-läuft)
9. [Home Assistant Anbindung](#home-assistant-anbindung)
10. [Fehlersuche](#fehlersuche)
11. [Bekannte Einschränkungen](#bekannte-einschränkungen)
12. [Changelog](#changelog)

---

## Funktionsprinzip

```
Home Assistant ──MQTT──> Broker ──WLAN──> Wemos D1 mini ──GPIO──> Optokoppler ──> Taste der FB ──Funk──> Haube
                                              │
                                              └── Quittung: …/last_command
```

- Pro Taste der Fernbedienung gibt es einen Optokoppler, dessen Ausgang parallel zum Taster
  der Fernbedienung gelötet ist.
- Ein MQTT-Befehl setzt den passenden GPIO für `PRESS_MS` (Standard 1000 ms) auf HIGH = „Taste gedrückt“.
- Befehle werden in eine Warteschlange (max. 8) gelegt und **nacheinander, nicht blockierend**
  abgearbeitet, mit `RELEASE_GAP_MS` Pause dazwischen.
- Nach jedem ausgeführten Tastendruck wird eine Quittung auf `…/last_command` gesendet.
- **Es gibt keine Rückmeldung von der Haube.** Die Firmware weiß nicht, ob die Haube an ist
  oder welche Stufe läuft – sie simuliert nur Tastendrücke (offener Regelkreis).

### Tasten der Fernbedienung

| Taste | Funktion an der Haube |
|---|---|
| **on**    | Einschalten bzw. eine Lüfterstufe höher |
| **off**   | Eine Lüfterstufe runter bzw. ausschalten |
| **light** | Licht an/aus (Toggle) |
| **timer** | Timer (Nachlauf) |

---

## Hardware

- **Wemos D1 mini** (ESP8266)
- 4 Optokoppler (Eingang: GPIO → Vorwiderstand → LED → GND; Ausgang: parallel zum jeweiligen
  Taster der Fernbedienung, Polung beachten)
- Original-Fernbedienung der Falmec Aura

### Pinbelegung

| Funktion | Pin Wemos | GPIO | Hinweis |
|---|---|---|---|
| on    | D2 | GPIO4  | ok |
| off   | D3 | GPIO0  | ⚠️ Boot-Pin, siehe unten |
| light | D5 | GPIO14 | ok |
| timer | D6 | GPIO12 | ok |

⚠️ **D3 = GPIO0** ist ein Boot-Strapping-Pin mit 10k-Pull-up auf dem Board. Er ist beim Booten
HIGH, bevor die Firmware läuft (→ möglicher „Off“-Druck bei jedem Reset), und eine Last gegen
GND kann Booten/Flashen stören. Beim nächsten Umlöten besser **D1 (GPIO5)** oder **D7 (GPIO13)**
verwenden und `PIN_OFF` in `config.h` anpassen. D4 und D8 ebenfalls meiden.

---

## Dateien im Repository

| Datei | Inhalt |
|---|---|
| `OptoCopler4CanWmodD1.ino` | Hauptprogramm: MQTT, Tasten-Warteschlange/Zustandsautomat, Watchdogs |
| `config.h`   | Alle Einstellungen: Topics, Pins, Zeiten, Optionen |
| `wmconfig.h` | WLAN-Setup über WiFiManager (Captive Portal), Speicherung der MQTT-Zugangsdaten im EEPROM, OTA-Setup |
| `README.md`  | Diese Dokumentation |

Benötigte Bibliotheken (Arduino Library Manager):
- **WiFiManager** (tzapu)
- **PubSubClient** (Nick O'Leary), ≥ 2.8 (wegen `setBufferSize`, `setKeepAlive`, `setSocketTimeout`)
- ESP8266 Board-Paket (enthält `ESP8266WiFi`, `ArduinoOTA`, `EEPROM`)

Board in der Arduino IDE: **LOLIN(WEMOS) D1 R2 & mini**.

---

## MQTT-Schnittstelle

Standard-Präfix `TOPICROOT` = `OptoCoppler2MQTT`.
`<devname>` = `OptoCoppler2MQTT-XXXXXX`, wobei `XXXXXX` die letzten 3 Bytes der MAC-Adresse
(Hex, Großbuchstaben) sind. Der konkrete Name steht im Seriellen Monitor beim Start und ist der
Hostname im Router / als OTA-Port.

### Befehle (Eingang)

| Topic | Payload | Wirkung |
|---|---|---|
| `OptoCoppler2MQTT/on`    | `1`, `on`, `true` oder `press` | Taste „on“ drücken |
| `OptoCoppler2MQTT/off`   | dto. | Taste „off“ drücken |
| `OptoCoppler2MQTT/light` | dto. | Taste „light“ drücken |
| `OptoCoppler2MQTT/timer` | dto. | Taste „timer“ drücken |

- Payload ist case-insensitive, leere oder andere Payloads werden ignoriert.
- Subscription: `OptoCoppler2MQTT/+` mit QoS 1.
- Die Befehls-Topics sind **nicht gerätespezifisch** (kein `<devname>` im Pfad).
- **Befehle immer OHNE Retain-Flag senden!** Sonst wird bei jedem Reconnect des ESP der letzte
  Befehl erneut ausgeführt.
- Mehrere Befehle schnell hintereinander (z. B. 3× `on` für Stufe 3) werden gepuffert und der
  Reihe nach gedrückt.

### Status (Ausgang)

| Topic | Payload | Retain | Bedeutung |
|---|---|---|---|
| `OptoCoppler2MQTT/<devname>/status` | `online` / `offline` | ja | Verfügbarkeit. `offline` ist das Last Will (LWT) bei Verbindungsabbruch. |
| `OptoCoppler2MQTT/<devname>/info` | `startup`, `version 0.1.0`, `ip 192.168.x.y`, `rssi -63`, `reconnect` | nein | Info-Meldungen beim (Re)Connect, alle auf demselben Topic → in Tools die History anschauen |
| `OptoCoppler2MQTT/<devname>/last_command` | `on`, `off`, `light`, `timer` – mit Zusatz ` (wake)` wenn ein Aufweck-Druck vorausging | nein | Quittung **nach** ausgeführtem Tastendruck |

Ablauf beim ersten Connect nach Boot auf `…/info`: `startup` → `version 0.1.0` → `ip …` → `rssi …`.
Bei späteren Reconnects: `reconnect` → `ip …` → `rssi …`.

---

## Konfiguration (`config.h`)

Änderungen erfordern neu kompilieren und flashen.

| Define | Standard | Bedeutung |
|---|---|---|
| `Topic_On/Off/Light/Timer` | `on`/`off`/`light`/`timer` | letzte Topic-Ebene der Befehle |
| `PIN_ON/OFF/LIGHT/TIMER` | D2/D3/D5/D6 | GPIOs der Optokoppler |
| `PRESS_MS` | 1000 | Wie lange eine Taste „gedrückt“ wird. Falls Drücke verloren gehen oder doppelt zählen: 200–400 testen |
| `RELEASE_GAP_MS` | 400 | Pause zwischen zwei gepufferten Tastendrücken |
| `WAKEUP_PULSE_ENABLED` | 0 | 1 = vor dem eigentlichen Druck einen kurzen Aufweck-Druck senden, wenn die FB länger als `REMOTE_IDLE_TIMEOUT_MS` nicht benutzt wurde |
| `WAKE_PULSE_MS` | 150 | Dauer des Aufweck-Drucks |
| `WAKE_GAP_MS` | 500 | Pause zwischen Aufweck-Druck und eigentlichem Druck |
| `REMOTE_IDLE_TIMEOUT_MS` | 10000 | Nach so langer Ruhe gilt die FB als „eingeschlafen“ |
| `MQTT_PORT` | 1883 | Broker-Port |
| `MQTT_KEEPALIVE_S` | 10 | MQTT-Keepalive, tote Verbindungen werden schneller erkannt |
| `MQTT_SOCKET_TIMEOUT_S` | 5 | Socket-Timeout |
| `MQTT_RECONNECT_MS` | 5000 | Abstand zwischen Reconnect-Versuchen (nicht blockierend) |
| `WIFI_PORTAL_TIMEOUT_S` | 180 | Config-Portal schließt nach dieser Zeit, danach Neustart |
| `WIFI_LOST_RESTART_MS` | 300000 | Neustart, wenn WLAN 5 min weg ist |
| `do_publishes` | true | false = keine Info/Quittungs-Nachrichten |

`TOPICROOT` und `DEBUGPRINT` können vor dem Include in der `.ino` überschrieben werden.

### Robustheits-Maßnahmen in der Firmware

- WLAN-Modem-Sleep aus (`WIFI_NONE_SLEEP`) – Hauptverdacht für „erster Befehl kommt nicht an“.
- GPIOs werden als allererstes im `setup()` auf LOW gesetzt (keine schwebenden Optokoppler).
- Tastendrücke laufen über einen nicht blockierenden Zustandsautomaten statt `delay()`.
- MQTT-Reconnect blockiert nicht; OTA bleibt immer erreichbar.
- Feste Client-ID (= `<devname>`), QoS-1-Subscription, kurzes Keepalive.
- WLAN-Watchdog und Timeout für das Config-Portal (Router-Ausfall blockiert nicht dauerhaft).

---

## Ersteinrichtung (WLAN / MQTT-Broker)

Beim allerersten Start (oder wenn der Wemos sein WLAN nicht findet) öffnet er einen Access Point
`OptoCoppler2MQTT-XXXXXX`:

1. Mit dem AP verbinden, das Captive Portal öffnet sich (sonst `http://192.168.4.1`).
2. WLAN auswählen, Passwort eingeben und die Felder **MQTT broker hostname**, **MQTT user name**,
   **MQTT password** ausfüllen.
3. Speichern → Wemos verbindet sich und speichert die Daten im EEPROM.

Das Portal schließt sich nach 180 s ohne Eingabe, der Wemos startet dann neu und versucht erneut.

**Werkseinstellungen:** Innerhalb der ersten ~1 Sekunde nach dem Start den Reset-Knopf drücken
→ beim nächsten Start werden die gespeicherten Einstellungen verworfen und das Portal öffnet sich.

⚠️ **Wichtig bei Updates:** Der EEPROM-Identifier ist `OptoCoppler2MQTT_<VERSION>`. Jede Änderung
von `VERSION` in der `.ino` macht die gespeicherten MQTT-Zugangsdaten ungültig → nach dem Flashen
öffnet sich das Config-Portal und Broker/User/Passwort müssen neu eingegeben werden.
Wer das vermeiden will, ändert in `wmconfig.h` `APPVERSION` auf einen festen Wert statt `VERSION`.

---

## Bauen & Flashen

### Kompilieren

Arduino IDE → Sketch-Ordner öffnen → *Überprüfen*. Eine Ausgabe wie
`Variables and constants in RAM … 40%`, `Instruction RAM … 92%`, `Code in flash … 33%`
bedeutet **Erfolg**. 92 % IRAM sind beim ESP8266 normal (32 KB davon sind fest als Cache reserviert).

Empfohlene Ordnerstruktur: `…\Arduino\OptoCopler4CanWmodD1\OptoCopler4CanWmodD1.ino` plus
`config.h` und `wmconfig.h` im selben Ordner.

### Upload per USB

*Werkzeuge → Port → COMx* wählen, hochladen.

Fehler `PermissionError(13, 'Ein an das System angeschlossenes Gerät funktioniert nicht.' …, 31)`:
- Port durch anderes Programm belegt (Serieller Monitor in zweitem Fenster, PuTTY …) → schließen
- USB ab- und anstecken, anderes Kabel / direkter USB-Port statt Hub
- CH340-Treiber im Geräte-Manager prüfen
- Upload Speed auf 115200 reduzieren
- Wemos testweise ohne Optokoppler-Platine flashen

### Upload per WLAN (OTA)

Unter *Werkzeuge → Port* erscheint ein Netzwerk-Port `OptoCoppler2MQTT-XXXXXX at 192.168.x.y`.

- **Kennwort-Abfrage:** Es ist kein OTA-Passwort gesetzt. Feld leer lassen oder beliebigen Text
  eingeben – wird ignoriert.
- **`[ERROR]: No response from device`:**
  - Windows-Firewall blockiert die Rückverbindung vom ESP → Python/espota/Arduino IDE für private
    Netzwerke freigeben, Netzwerkprofil auf „Privat“
  - PC und Wemos im selben Netz (kein Gäste-WLAN / VPN)
  - IDE neu starten, falls die IP veraltet ist

---

## Prüfen, ob die Firmware läuft

Mit einem MQTT-Client (MQTT Explorer, HA → Einstellungen → Geräte & Dienste → MQTT →
*MQTT-Einstellungen* → „Auf ein Topic hören“) `OptoCoppler2MQTT/#` abonnieren und den Wemos
neu starten:

- `…/status` = `online`
- `…/info` History: `startup`, `version 0.1.0`, `ip …`, `rssi …`
- Test: `OptoCoppler2MQTT/light` ← `1` (ohne Retain) → Licht schaltet, ~1 s später `…/last_command` = `light`

Seriell (115200 Baud): `OptoCoppler2MQTT Version 0.1.0`, `Message arrived [...]`, `Press light`.

---

## Home Assistant Anbindung

Die Firmware hat **kein MQTT-Discovery**; Entitäten werden manuell per YAML angelegt.
`XXXXXX` durch den echten Gerätenamen ersetzen (siehe `…/status`-Topic im MQTT Explorer).

### `configuration.yaml` (bzw. `mqtt:`-Package)

```yaml
mqtt:
  button:
    - name: "Dunstabzug Stufe hoch"
      unique_id: dunstabzug_on
      command_topic: "OptoCoppler2MQTT/on"
      payload_press: "1"
      retain: false
      qos: 1
      icon: mdi:fan-plus
      availability_topic: "OptoCoppler2MQTT/OptoCoppler2MQTT-XXXXXX/status"
      payload_available: "online"
      payload_not_available: "offline"
      device: &dunstabzug_device
        identifiers: ["dunstabzug_falmec_aura"]
        name: "Dunstabzug"
        manufacturer: "Falmec"
        model: "Aura (Fernbedienung via ESP8266)"

    - name: "Dunstabzug Stufe runter / Aus"
      unique_id: dunstabzug_off
      command_topic: "OptoCoppler2MQTT/off"
      payload_press: "1"
      retain: false
      qos: 1
      icon: mdi:fan-minus
      availability_topic: "OptoCoppler2MQTT/OptoCoppler2MQTT-XXXXXX/status"
      payload_available: "online"
      payload_not_available: "offline"
      device: *dunstabzug_device

    - name: "Dunstabzug Licht"
      unique_id: dunstabzug_light
      command_topic: "OptoCoppler2MQTT/light"
      payload_press: "1"
      retain: false
      qos: 1
      icon: mdi:lightbulb
      availability_topic: "OptoCoppler2MQTT/OptoCoppler2MQTT-XXXXXX/status"
      payload_available: "online"
      payload_not_available: "offline"
      device: *dunstabzug_device

    - name: "Dunstabzug Timer"
      unique_id: dunstabzug_timer
      command_topic: "OptoCoppler2MQTT/timer"
      payload_press: "1"
      retain: false
      qos: 1
      icon: mdi:timer-outline
      availability_topic: "OptoCoppler2MQTT/OptoCoppler2MQTT-XXXXXX/status"
      payload_available: "online"
      payload_not_available: "offline"
      device: *dunstabzug_device

  sensor:
    - name: "Dunstabzug letzter Befehl"
      unique_id: dunstabzug_last_command
      state_topic: "OptoCoppler2MQTT/OptoCoppler2MQTT-XXXXXX/last_command"
      icon: mdi:remote
      device: *dunstabzug_device

    - name: "Dunstabzug Info"
      unique_id: dunstabzug_info
      state_topic: "OptoCoppler2MQTT/OptoCoppler2MQTT-XXXXXX/info"
      icon: mdi:information-outline
      entity_category: diagnostic
      device: *dunstabzug_device

  binary_sensor:
    - name: "Dunstabzug Controller online"
      unique_id: dunstabzug_online
      state_topic: "OptoCoppler2MQTT/OptoCoppler2MQTT-XXXXXX/status"
      payload_on: "online"
      payload_off: "offline"
      device_class: connectivity
      entity_category: diagnostic
      device: *dunstabzug_device
```

Hinweis: YAML-Anker (`&`/`*`) funktionieren nur innerhalb **derselben** Datei.

### Beispiel-Skripte (mehrere Stufen)

```yaml
script:
  dunstabzug_stufe_2:
    alias: "Dunstabzug Stufe 2"
    sequence:
      - action: button.press
        target: { entity_id: button.dunstabzug_stufe_hoch }
      - action: button.press
        target: { entity_id: button.dunstabzug_stufe_hoch }

  dunstabzug_aus:
    alias: "Dunstabzug aus"
    description: "Drückt 4x 'off' - genug, um von jeder Stufe aus auszuschalten"
    sequence:
      - repeat:
          count: 4
          sequence:
            - action: button.press
              target: { entity_id: button.dunstabzug_stufe_runter_aus }
```

Die Firmware puffert die Drücke, eine Verzögerung zwischen den `button.press` ist nicht nötig.
Die Anzahl Stufen der Haube und das exakte Verhalten von „off“ (eine Stufe runter vs. sofort aus)
bitte an der realen Haube verifizieren und die Skripte anpassen.

### Anbindung an Home Assistant via MCP

Für die Steuerung durch einen KI-Assistenten über die HA-Integration **„Model Context Protocol Server“**:

- Die Integration stellt nur Entitäten bereit, die für **Assist freigegeben** sind
  (Einstellungen → Sprachassistenten → Entitäten freigeben). Die Buttons, Skripte und ggf. der
  Sensor `last_command` müssen dort freigegeben werden.
- Sprechende Namen/Aliase vergeben (z. B. „Dunstabzug Licht“, „Dunstabzug Stufe hoch“), damit der
  Assistent sie eindeutig zuordnen kann.
- Für den Assistenten relevant: Es gibt **keinen Zustand** der Haube (an/aus, Stufe). Er kann nur
  Tasten drücken. Für „mach die Haube aus“ das Skript `dunstabzug_aus` freigeben; ob ein Druck
  ausgeführt wurde, zeigt `sensor.dunstabzug_letzter_befehl`.
- `binary_sensor.dunstabzug_controller_online` zeigt, ob der ESP erreichbar ist.

---

## Fehlersuche

### „Erster Befehl wird nicht ausgeführt“ (ursprüngliches Problem)

Haube eine Weile in Ruhe lassen (≥ 30 min), dann **einen** Befehl senden und `…/last_command` beobachten:

| Beobachtung | Ursache | Maßnahme |
|---|---|---|
| Haube schaltet sofort | Behoben (WLAN-Modem-Sleep war die Ursache) | – |
| `last_command` kommt, Haube reagiert nicht | Fernbedienung schläft / Druck wird nicht erkannt | `WAKEUP_PULSE_ENABLED 1`; `PRESS_MS` auf 200–400; FB-Batterie; Optokoppler-Polung/Vorwiderstand prüfen |
| Kein `last_command` | Befehl erreicht den ESP nicht | `…/info` auf `reconnect` prüfen, RSSI (< −75 dBm schlecht), Broker-Log, Befehl wirklich auf `OptoCoppler2MQTT/<cmd>` gesendet? |

Gegenprobe: Reagiert die Haube auch mit der **echten** Fernbedienung nach längerer Pause erst
beim zweiten Druck, liegt es an der FB/Haube → Aufweck-Druck aktivieren.

### Ungewollte Tastendrücke

- Beim Neustart des ESP: Retained-Nachrichten auf den Befehls-Topics? Im MQTT Explorer löschen
  und in HA `retain: false` setzen.
- „Off“ beim Reset: D3/GPIO0-Problematik, siehe [Pinbelegung](#pinbelegung).

---

## Bekannte Einschränkungen

- Keine Zustandsrückmeldung der Haube (nur simulierte Tastendrücke).
- Befehls-Topics sind nicht gerätespezifisch – bei mehreren solchen Geräten am selben Broker
  `TOPICROOT` pro Gerät ändern.
- Kein MQTT-Discovery für Home Assistant (manuelle YAML-Konfiguration).
- MQTT-Zugangsdaten werden im Klartext im Seriellen Log ausgegeben.
- Kein OTA-Passwort gesetzt.

---

## Changelog

### 0.1.0
- WLAN-Modem-Sleep deaktiviert, Auto-Reconnect
- Pins werden zu Beginn von `setup()` auf LOW gesetzt
- Nicht blockierende Tasten-Warteschlange statt `delay()` im MQTT-Callback
- Optionaler Aufweck-Druck (`WAKEUP_PULSE_ENABLED`)
- Subscription nur `TOPICROOT/+` mit QoS 1, feste Client-ID, kürzeres Keepalive
- Nicht blockierender MQTT-Reconnect, WLAN-Watchdog, Timeout für das Config-Portal
- `…/status` nur noch `online`/`offline`; neue Topics `…/info` und `…/last_command`
- Leere Payloads werden ignoriert; zusätzlich `on`/`true`/`press` als Payload erlaubt
- Fix: IP-Adresse in der Startmeldung war nicht initialisiert
- Fix: Compiler-Warnungen `-Wwrite-strings`

### 0.0.1
- Ursprüngliche Version

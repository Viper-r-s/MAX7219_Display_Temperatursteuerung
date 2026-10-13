# MAX7219 Display mit lokaler Temperaturregelung

Ein Sketch für zwei eigenständige ESP8266/MAX7219-Displays (Holz- und Metallwerkstatt). Jeder ESP8266 regelt seinen **Heizlüfter lokal** anhand seines DHT22. WLAN und MQTT werden nur für Meldungen, Außentemperatur und optionale Fernvorgaben benötigt.

## Vor dem Flashen

In `MAX7219_Display1/MAX7219_Display1.ino` `DISPLAY_NUMBER` auf `1` (Holz) oder `2` (Metall) setzen und denselben Sketch auf beide Geräte flashen. Daraus ergeben sich MQTT-Pfad `display1`/`display2` und der Hostname automatisch. Board: LOLIN (WEMOS) D1 R2 & mini (ESP8266). Bibliotheken: Adafruit GFX, Max72xxPanel, PubSubClient und DHT sensor library.

Verdrahtung aus dem bisherigen Sketch: DHT22 GPIO0; Relais GPIO5; Bewegungsmelder GPIO4; Taster Hoch GPIO3 und Runter GPIO2 (gegen GND, interne Pull-ups); Matrix DIN D7, CS D8, CLK D5. GPIO2 muss zum Booten HIGH sein; beim Einschalten den Runter-Taster nicht gedrückt halten.

`COOLING_MODE = false` und `RELAY_ON_LEVEL = HIGH`: Der Heizlüfter schaltet bei 8 °C Sollwert und 2 °C Hysterese bei **6 °C oder weniger ein** und bei **8 °C oder mehr aus**. Dazwischen bleibt der bisherige Zustand erhalten. Bei fehlenden gültigen DHT22-Werten über 15 Sekunden schaltet das Relais aus. Die Taster ändern den Sollwert lokal in 0,5-°C-Schritten. Nach jedem Neustart gelten wieder 8 °C Sollwert und 2 °C Hysterese; Änderungen werden nicht gespeichert.

## MQTT

Der MQTT-Client verbindet sich wie bisher ohne Benutzer/Passwort mit dem Broker. Die Verbindung wird wiederholt versucht, ohne im `setup()` auf WLAN zu warten. Die lokale Regelung läuft auch ohne Verbindung. Auf einem Kaltstart ohne WLAN/NTP zeigt die Uhr `--:--`, bis eine Zeit synchronisiert wurde; die Temperaturregelung ist davon unabhängig.

Für das jeweilige Display (`display1` oder `display2`):

| Richtung | Topic | Inhalt |
| --- | --- | --- |
| Eingang | `displayN/solltemperatur/set` | Solltemperatur 0–40 °C; wird auf 0,5 °C gerundet |
| Eingang | `displayN/hysterese/set` | Hysterese 0,1–10,0 °C; wird auf 0,1 °C gerundet |
| Ausgang | `displayN/solltemperatur` | Aktueller Sollwert, retained |
| Ausgang | `displayN/hysterese` | Aktuelle Hysterese, retained |
| Ausgang, wie bisher | `displayN/relais`, `/temperature`, `/humidity`, `/bewegung`, `/hoch`, `/runter`, `/LWT` | Relaiszustand, Messwerte und Ereignisse |

Beispiele: `mosquitto_pub -h 192.168.1.200 -t display1/solltemperatur/set -m 9.5` und `mosquitto_pub -h 192.168.1.200 -t display1/hysterese/set -m 1.5`. Befehle an `/set` **nicht retained** senden, damit nach einem Neustart nicht automatisch ein alter Wert wieder eingespielt wird. Der alte MQTT-Eingang `displayN/relais` schaltet das Relais absichtlich nicht mehr; dessen Status wird weiterhin gesendet. Der Zustand des anderen Relais wird nur zum kurzzeitigen Abschalten der Matrix bei Schaltstörungen empfangen. `displayN/text`, `/speed`, `/scroll`, `/intensity`, `/abstand` bleiben für kurzzeitige Textmeldungen und Anzeigeeinstellungen erhalten. Die Daueranzeige (Uhr, Soll-, Innen- und Außentemperatur) wird lokal aufgebaut. Außentemperatur kommt optional weiter von `tele/schalter2/SENSOR`; sie wird nicht für die Regelung benutzt.

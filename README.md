# MAX7219 Display mit lokaler Temperaturregelung

Ein Sketch für zwei eigenständige ESP8266/MAX7219-Displays (Holz- und Metallwerkstatt). Jeder ESP8266 regelt seinen **Heizlüfter lokal** anhand seines DHT22. WLAN und MQTT werden nur für Meldungen, Außentemperatur und optionale Fernvorgaben benötigt.

## Vor dem Flashen

In `MAX7219_Display1/MAX7219_Display1.ino` `DISPLAY_NUMBER` auf `1` (Holz) oder `2` (Metall) setzen und denselben Sketch auf beide Geräte flashen. Daraus ergeben sich MQTT-Pfad `display1`/`display2` und der Hostname automatisch. Board: LOLIN (WEMOS) D1 R2 & mini (ESP8266). Bibliotheken: Adafruit GFX, Max72xxPanel, PubSubClient und DHT sensor library.

Verdrahtung aus dem bisherigen Sketch: DHT22 GPIO0; Relais GPIO5; Bewegungsmelder GPIO4; Taster Hoch GPIO3 und Runter GPIO2 (gegen GND, interne Pull-ups); Matrix DIN D7, CS D8, CLK D5. GPIO2 muss zum Booten HIGH sein; beim Einschalten den Runter-Taster nicht gedrückt halten.

`COOLING_MODE = false` und `RELAY_ON_LEVEL = HIGH`: Der Heizlüfter schaltet bei 8 °C Sollwert und 2 °C Hysterese bei **7 °C oder weniger ein** und bei **9 °C oder mehr aus**. Die Hysterese bezeichnet die gesamte Spanne zwischen beiden Schaltpunkten und liegt symmetrisch um den Sollwert. Dazwischen bleibt der bisherige Zustand erhalten. Bei fehlenden gültigen DHT22-Werten über 15 Sekunden schaltet das Relais aus. Die Taster ändern den Sollwert lokal in 1-°C-Schritten: ein Schritt beim Drücken, nach 550 ms Halten weitere Schritte alle 400 ms. Der Sollwert startet nur beim ersten Mal mit 8 °C; jede spätere Änderung per Taster oder MQTT wird sofort im ESP-Flash gespeichert und nach einem Neustart geladen. Die Hysterese startet nach jedem Neustart weiterhin mit 2 °C.

## MQTT

Der MQTT-Client verbindet sich wie bisher ohne Benutzer/Passwort mit dem Broker. Die Verbindung wird wiederholt versucht, ohne im `setup()` auf WLAN zu warten. Die lokale Regelung läuft auch ohne Verbindung. Auf einem Kaltstart ohne WLAN/NTP zeigt die Uhr `--:--`, bis eine Zeit synchronisiert wurde; die Temperaturregelung ist davon unabhängig.

Für das jeweilige Display (`display1` oder `display2`):

| Richtung | Topic | Inhalt |
| --- | --- | --- |
| Eingang | `DisplayN/Solltemperatur` | Sollwert direkt aus dem Smart-Home-System; `N` ist 1 oder 2 (Groß-/Kleinschreibung beachten) |
| Eingang | `displayN/Solltemperatur` | Zusätzlich unterstützte Variante mit kleinem `d` |
| Eingang | `displayN/solltemperatur/set` | Alternativer Sollwert-Befehl; 0–40 °C, auf ganze Grad gerundet |
| Eingang | `displayN/hysterese/set` | Hysterese 0,1–10,0 °C; wird auf 0,1 °C gerundet |
| Ausgang | `displayN/solltemperatur` | Aktueller Sollwert, retained |
| Ausgang | `displayN/hysterese` | Aktuelle Hysterese, retained |
| Ausgang, wie bisher | `displayN/relais`, `/temperature`, `/humidity`, `/bewegung`, `/hoch`, `/runter`, `/LWT` | Relaiszustand, Messwerte und Ereignisse |

Beispiele: `mosquitto_pub -h 192.168.1.200 -t Display1/Solltemperatur -m 9` und `mosquitto_pub -h 192.168.1.200 -t display1/hysterese/set -m 1.5`. Das Smart-Home-System muss keinen zusammengesetzten Temperaturtext mehr an `displayN/text` senden: Die Daueranzeige (Uhr, Soll-, Innen- und Außentemperatur) wird lokal aufgebaut und aktualisiert. Tasteränderungen werden auch retained an `DisplayN/Solltemperatur` gemeldet. Bei WLAN-Ausfall werden sie gespeichert und vor dem Abonnement des primären retained MQTT-Sollwerts nachgesendet. Wenn es keine ausstehende lokale Änderung gibt, kann ein neuer retained Smart-Home-Wert nach dem Verbinden übernommen werden. Für die alternativen Eingänge `displayN/Solltemperatur` und `displayN/solltemperatur/set` keine retained Befehle verwenden, damit alte Befehle nach dem Neustart keinen gespeicherten Sollwert überschreiben. Der alte MQTT-Eingang `displayN/relais` schaltet das Relais absichtlich nicht mehr; dessen Status wird weiterhin gesendet. Der Zustand des anderen Relais wird nur zum kurzzeitigen Abschalten der Matrix bei Schaltstörungen empfangen. `displayN/text`, `/speed`, `/scroll`, `/intensity`, `/abstand` bleiben für optionale Textmeldungen und Anzeigeeinstellungen erhalten. Außentemperatur kommt optional weiter von `tele/schalter2/SENSOR`; sie wird nicht für die Regelung benutzt.

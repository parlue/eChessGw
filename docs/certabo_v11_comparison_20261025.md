# Certabo: eingefrorene V11 gegen Spectrum-Entwicklung, 25.10.2026

> Nachtrag 26.10.2026: Die unten historisch beschriebene Tagfamilien-Erkennung
> und der Praefix-Rauschfilter wurden entfernt. Figuren sind zwischen Brettern
> austauschbar. Aktueller Stand: [certabo_spectrum.md](certabo_spectrum.md).

Referenz ist die echte eingefrorene Umgebung `esp32-c3-superminiV11_Certabo`,
mit `gateway_v11_certabo.cpp`, `certabo_board_v11.cpp` und den anderen
`*_v11`-Kopien. Nicht nur ein beliebiger älterer Entwicklungsstand.
Der Freeze selbst wurde nicht verändert.

## Belegte Unterschiede

| Bereich | V11 | Heutige Entwicklung | Bezug zum Verbindungsfehler |
|---|---|---|---|
| BLE-Name | Certabo | Certabo und zusätzlich Spectrum | Certabo-Matching selbst unverändert |
| Scan | aktiv, 5 s | identisch; zusätzlich Log-App suchen | Log-Upload erst am Ende des Fehlerlaufs sichtbar |
| Connect-Task | 8192 Byte, Priorität 1 | identisch | kein geänderter Taskparameter |
| Erster Verbindungsversuch | MTU 256, synchrones `connect(address)` | identisch | neuer LED-Decoder hier noch nicht aufgerufen |
| NUS | Service 0001, Write-NR 0002, Notify 0003 | identisch | aktueller Abbruch liegt vor erfolgreicher Subscription |
| Software-Angebot bei Kabelaktivität | bereits vorhanden | gleicher Code plus Log-Wrapper | Parallelbetrieb existierte schon; unterschiedliche Startreihenfolge in Mitschnitten beweist keine Code-Regression |
| Logging | USB-Serial | USB plus RAM-Ring, optional Flash und BLE-Abruf | globale Erweiterung gegenüber Freeze; aktueller Lauf startet mit rec=0 |
| Spectrum-Initialisierung | fehlt | Filter zurücksetzen; bisher wurden auch Einstellungen aus NVS gelesen | NVS-Zugriff lokal auf bestätigtes RGB nach Verbindung verschoben |
| Protokollparser | CR/LF-Parser | gleiche Parserlogik | keine Spectrum-bedingte Änderung des Framing im finalen lokalen Stand |
| LED-Typ | Parser-L/D | Tag-Klassifikation, Marker und Paketfallback | läuft erst nach empfangenen Daten |
| Spectrum-Ausgabe | alte generische RGB-Funktion | separater 81-LED-Encoder, Gesten, Rauschfilter | erst nach erfolgreicher Verbindung aktiv |
| iChessOne | älterer Treiber | eigene LED-/Gestenänderungen | nicht der gewählte Bretttreiber beim Certabo-Test |

Die zusätzliche `notificationLength` im Queue-Paket nutzt das vorherige Padding:
beide Strukturen sind auf dem ESP32-C3 136 Byte groß. Die Queue bleibt 32 Einträge.

## Logs

Erfolgreiche Mitschnitte 11.10.: Subscription nach etwa 0,85 / 0,90 s bei MTU256,
danach 34 IDs und Spielstellungen. Aktueller Mitschnitt 16:45: erster Treffer nach
53 s; wiederholt HCI-Verbindungsaufbaufehler 574 (0x200+0x3e), einmal Verbindung
mit MTU23 und danach Timeout 520 (0x200+0x08). Fehler 7 bei NUS-Suche heißt dann
nicht verbunden. Beide Dateien 16:45 / 16:46 sind bytegleich.

Die unterschiedlichen MAC-Adressen sind keine Bretttyp- oder Modulkennung.
Laut Nutzer verwenden Certabo und Spectrum wegen unterschiedlicher USB-Chips
zwei BLE-Module von Pietro, beide mit demselben Namen Certabo. Die früheren
unvollständigen Daten gehörten zum falsch eingesetzten BLE-Modul; daraus wird
kein Framingfehler des aktuellen Aufbaus abgeleitet.

## Bibliotheken und Build

Die V11-Umgebung erbt Plattform und Bibliotheksanforderung aus der Basis:
`espressif32` ohne Pin und NimBLE `^2.5.0`. Der aktuelle Build löst 7.0.1,
Arduino 2.0.17 und NimBLE 2.5.1 auf. Die exakte aufgelöste NimBLE-Version der
alten Binärdatei wurde nicht archiviert; gleiche Anforderung ist kein Beweis
für gleiche Bibliotheksbytes. Alte und neue Binärdateien enthalten dieselbe
ESP-IDF-Kennung v4.4.7 38eeba213a. Der Freeze pinnt seine Abhängigkeiten nicht.

## Vertiefter Binärvergleich

Auch die tatsächlichen Maschinenbefehle wurden gegen die alte Snapshot-Binärdatei
und die öffentliche stabile V11 verglichen. Verbindungsfunktion, Client-Konstruktor,
Start des Verbindungsversuchs, Verbindungs-Timer, GAP-Ereignisbehandlung und Scan-Start
stimmen bei ausgeblendeten Relokationsadressen überein. Das beweist nicht die
Identität der gesamten Bibliothek oder aller Daten, grenzt einen veränderten Code
in genau diesen Funktionen aber ein. Reproduzierbar mit
`tools/compare_ble_binary.py`; Ergebnis `ble-function-comparison.json` im Diagnoseordner.

## Lokaler nächster Teststand (aktualisiert nach Nutzerfestlegung)

MTU256 bleibt fest. Der lokale MTU185-Versuch wurde verworfen. Auch der zusätzliche
Retry ohne MTU-Austausch wurde entfernt: wieder genau ein Anwendungsaufruf
`connect(address)` wie im Freeze (interne Bibliothekswiederholungen unverändert).
Spectrum-Einstellungen werden erst nach bestätigtem RGB-Empfang geladen;
ein Standard-Certabo führt vor seinem Verbindungsversuch keinen Spectrum-NVS-Zugriff aus.
Zusätzlich: originale Werbedaten, RSSI, Connectable-Flag, paralleles Advertising,
NimBLE-Version, Connection-Intervall und Timeout im Log. Keine Namens- oder
MAC-basierte Auswahl des USB-Chips. Die Parser-Erweiterung ohne CR/LF wurde
nach der Klarstellung des Nutzers wieder herausgenommen.

Supermini-Build erfolgreich. Dieser Stand ist lokal; keine Veröffentlichung oder
Flash im Rahmen dieses Vergleichs. Die entfernte Kopplung ist eine begrenzte
Lifecycle-Korrektur, als Ursache des HCI-Verbindungsfehlers aber nicht nachgewiesen.

## Ergebnis und belastbare Grenze

Der Vergleich hat noch keine bewiesene Ursache der BLE-Regression ergeben.
Die eigentliche Spectrum-Erkennung kann erst nach der Verbindung arbeiten;
sie erklärt die geloggten Fehler beim Verbindungsaufbau nicht direkt.
Die zwei BLE-Module lassen sich vor dem Verbindungsaufbau nur unterscheiden,
wenn sie unterschiedliche Werbedaten liefern. Solche Daten fehlen in den
alten Mitschnitten. Der App-Treiber erkennt nach Empfang anhand der beobachteten
Tagfamilien (2,0 gegenüber 93/94); das ist bereits im Gateway übernommen.

Ein Gegentest mit der unveränderten alten Binärdatei am selben Aufbau bleibt
der entscheidende Nachweis einer Firmware-Regression. Die alten erfolgreichen
Logs und aktuellen Fehlerlogs allein ersetzen diesen kontrollierten Test nicht.
Bis dahin keine Behauptung, ein bestimmter USB-Chip, MTU oder Advertising sei
als Ursache nachgewiesen.

Reproduzierbarer Quelldiff samt Hashes: `tools/audit_certabo_v11.py` erzeugt
`output/diagnostics/certabo-v11-comparison-20261025/`.

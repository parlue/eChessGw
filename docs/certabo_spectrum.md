# Certabo / Spectrum: Auswahl durch Zusatzdamen (27.10.2026)

Ziel: ein gemeinsamer BLE-, Parser- und Spielpfad. Nur der LED-Treiber wird
ueber eine ausdrueckliche Aufstellentscheidung gewaehlt.

| Brett | Aufstellung zum Einlernen | Ausgabe nach erfolgreichem Einlernen |
|---|---|---|
| Certabo, normale LEDs | Grundstellung + weisse Zusatzdame d3, schwarze d6 | 8-Byte-LED-Maske |
| Spectrum, 81 RGB-LEDs | Grundstellung + weisse Zusatzdame e3, schwarze e6 | 247-Byte-RGB-Frame; Startblinken Blau 80 % |

## Verbindlicher Startablauf

1. BLE verbinden, Nordic-UART-Notifications abonnieren und Stellungsteile
   zusammensetzen. Kein Startkommando und keine LED-Probe senden.
2. Jede vollstaendige Stellung erreicht die Kalibrierung. Es werden sieben
   Meldungen mit feldweiser strenger Mehrheit ausgewertet. Alle 32 regulaeren
   Felder muessen besetzt sein, nur eines der beiden Zusatzdamen-Paare darf
   besetzt sein, und die 34 IDs muessen eindeutig sein.
3. Bis zum erfolgreichen Abschluss bleibt der LED-Typ Unknown. sendLeds()
   verwirft jeden Schreibversuch in diesem Zustand; auch Differenzen beim
   Aufbau werden in dieser Phase NICHT angezeigt.
4. Erst die erfolgreiche Kalibrierung legt den Treiber fest. d3/d6 bedeutet
   Mono, e3/e6 bedeutet Spectrum. Das ist eine Positionswahl durch den Nutzer,
   keine Klassifikation von Chipnummern.
5. Vier Mittelfelder d4/e4/d5/e5 blinken dreimal, 700 ms pro Phase.
   Spectrum verwendet dabei unabhaengig von gespeicherten Gesten Blau 80 %.
6. Anschliessend zeigen die LEDs Abweichungen von der regulaeren Grundstellung,
   darunter die verbliebenen Zusatzdamen auf d3/d6 bzw. e3/e6.
7. Erst nach Entfernung beider Zusatzdamen und korrekter Grundstellung werden
   normale Stellungen an den Host freigegeben.

Fehlende Figuren, doppelte IDs, gemischte d/e-Paare oder zusaetzlich besetzte
Felder geben keinen Treiber frei. Eine einzelne verrauschte Meldung kann durch
die bestehende Sieben-Scan-Mehrheit ausgefiltert werden. Unbekannte oder doppelte
IDs im laufenden Spiel halten die ganze Stellung zurueck, niemals als leer.

## Auswahl bleibt fuer diese Verbindung fest

Paketgroesse, ausgehandelte MTU, L/D-Antworten, MAC und ID-Praefixe beeinflussen
die Auswahl nicht. Beim Neuverbinden werden Typ und Figurenkalibrierung geleert;
alle 34 Figuren werden erneut eingelesen. Gespeicherte Figurenarchive dienen nur
Diagnose/Simulation und enthalten keine wiederzuverwendende aktive Brettwahl.
Die MTU-Anforderung bleibt 256. King/Phoenix-Kabelverarbeitung bleibt unveraendert.

Die bisherige 64-Byte-Regel wurde entfernt: auch funktionierende Mono-Certabos
senden 64-Byte-Positionsanfaenge. In Pietros RGB-Beschreibung ist D eine Antwort
NACH einem vollstaendigen RGB-Schreibframe, keine dokumentierte Vorab-Abfrage.

## Spectrum: Farbe und Helligkeit

Standard im Spiel ist Blau 80 % (0,0,204). Bereits gespeicherte ausdrueckliche
Gesten-Einstellungen werden weiter geladen; das Startblinken bleibt Blau 80 %.
Normaler Aufbau plus genau eine eingelesene weisse Zusatzdame fuer eine Sekunde:

| Feld | Funktion |
|---|---|
| g3 / g4 / g5 / g6 | Rot / Blau / Gruen / Orange |
| h3 / h4 / h5 / h6 | 25 / 50 / 75 / 100 % Helligkeit |

Drei Mittelfeldblinker bestaetigen die Geste. Danach Dame entfernen. Diese
Konfigurationsstellung wird nicht als Spielzug an den Host gesendet.

RGB-Format unveraendert: FF55 + 81 RGB-Tripel + CRLF. Vier Ecken je Feld;
reserviertes FF55 im Farb-Payload wird vermieden. Der Mono-Treiber behaelt seine
acht Bytes. Die transportseitige Schreibreihenfolge bleibt unveraendert.

## Pruefung / Rueckweg

`tools/test_spectrum_alignment.cmd` prueft sieben Gruppen, einschliesslich der
neuen Aufstellwahl mit denselben Chips, 1/20/64/253-Byte-Fragmentierung,
widersprechenden L/D-Meldungen, gemischten/fehlenden/doppelten Figuren,
Damenentfernung, Blau80-Blinken, Archiv-Isolation und Neuverbindung.
Build: `pio run -e esp32-c3-supermini`. Physische Hardwaretests stehen aus.
Sicherung vor der Aenderung: `backups/queen-layout-before-20261027/`.
Die Korrektur der Auswahl beweist keinen Fix eines Funkabbruchs vor NUS-Anmeldung.

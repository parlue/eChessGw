# eChessGW_XMasGift - 19. November 2026

Isolierte ESP32-C3-SuperMini-Variante mit Virgin Adaptive. Der vorherige
Gateway ist als **V14_stable** eingefroren und als eigene Firmware gesichert.

## Virgin 0.8.7 (20. November)

Der Enginekern wurde aus `Documents/Projects/virgin/src` auf 0.8.7 Beta
aktualisiert. `src/xmasgift/virgin/UPSTREAM_0.8.7.json` haelt die importierten
Quellpruefsummen fest. Der vorherige vollstaendige XmasGift-Quellstand und
sein gebautes Image liegen unter `backups/xmasgift-before-virgin087-20261120`.

Das eingebettete Buch (90 Varianten, zehn Halbzuege) ist fuer direkte Partien
aktiv. Nach Abweichung/Buchende uebernehmen Suche und adaptive Auswahl von
0.8.7. Die sichere Zufallsauswahl verwendet zwei unabhaengige Zufallswerte.
Keine externe Buchdatei ist erforderlich.

F4, normales Einlernen, LED-Bewertung und permanente Ergebnisanzeige bleiben
erhalten. Die C3-Anpassungen bleiben lokal: Suchspeicher nur pro Suche,
20-Ply-Grenze, 56 KiB Taskstack, 8 KiB nichtwerfender Hash, kompakter
Countermove-Cache und wiederverwendete Quiet-Liste. Die neue Capture-History
aus 0.8.7 wird ebenfalls pro Suche allokiert; die Speicherfreigabe vor dem
Start berechnet ihre Groesse mit ein. Ein S3-Firmwareimage wird nicht auf
den C3 uebernommen.

Validierung der Integration: Native Tests mit Exitcode 0 bestanden, inklusive
aller 90 Buchlinien, adaptiver 0.8.7-Regeln, Suchregressionen, F4-/Resetablauf
und permanenter Ergebnisanzeige. C3-Build erfolgreich: 1.100.400 Byte Flash
von 1.310.720 (84,0 %), 106.036 Byte statisches RAM. Separates Image und
Pruefsummen: `output/xmasgift-virgin087-20261120/BUILD.json`.
Am 20. November auf den identifizierten C3-SuperMini geflasht; esptool hat die
Flash-Pruefsumme bestaetigt. Anschliessend bestanden die expliziten
synthetischen Geraetetests mit 0.8.7: beide Menschenfarben, je vier Halbzuege,
Buchzug und echte Suche nach Buchabweichung sowie Reset waehrend einer Suche.
Danach jeweils phase=0, own=0, job=0 und freigegebener Suchspeicher.
Eine beobachtete Suche erreichte Tiefe 4, fuenf Kandidaten, 5477 Knoten in
2453 ms mit 27820 Byte gemessener Stackreserve. Dies ist keine allgemeine
Stack-/Laufzeitgarantie. Noch kein neuer Test mit echtem Brett unter 0.8.7;
die bestaetigten Chessnut-/Certabo-Partien stammen vom vorherigen Kern.

## Bedienung

1. Ohne Kabelcomputer starten und das Brett verbinden. Bei Certabo/Spectrum
   das normale Einlernen aller 34 Figuren abschliessen und Zusatzdamen entfernen.
   Verbindung, Einlernen und Bereitschaftssignal muessen abgeschlossen sein.
   Danach waehlt F4 direkt Virgin; eine Maskierung ist dafuer NICHT erforderlich.
   A4 (ChessLink) und B4 (Chessnut) bleiben alternative Gesten fuer den normalen
   Gateway-Betrieb. Eine virtuelle Certabo-App ist die Brett-Gegenstelle.
2. Normale Grundstellung plus **zweite weisse Dame auf F4**:
   Mensch spielt Weiss. **Zweite schwarze Dame auf F4**: Mensch spielt Schwarz,
   Virgin eroeffnet mit Weiss. Keine Stammfigur versetzen.
3. Nach stabiler Erkennung und erfolgreicher Vorbereitung blinken die vier
   Mittelfelder zweimal, danach die vier Eckfelder zweimal (je 250 ms an/aus).
4. Zusatzdame wieder entfernen. Erst wenn auch das Signal abgeschlossen ist
   und die Grundstellung erkannt wird, beginnt die Partie.
5. Menschenzuege normal am Brett ausfuehren; Virgins angezeigte Zuege ebenfalls
   am Brett ausfuehren. Ein Robotertarget wird einmalig als genaue Stellung
   beziehungsweise legaler UCI-Zug uebergeben, ohne automatische Wiederholung.
6. Nach mindestens einem bestaetigten legalen Zug beendet die spaetere,
   400 ms stabil erkannte Grundstellung die Partie und eine laufende Suche.
   Fuer die naechste Partie wieder eine F4-Geste verwenden.

Die anfaengliche Grundstellung und doppelte Meldungen waehrend Virgins
Eroeffnung brechen nichts ab. Das Anheben/Zurueckstellen einer Figur ist
kein bestaetigter Zug. Andere Abweichungen von der Grundstellung starten nichts.
Bei aktivem Kabelcomputer oder externer BLE-Steuerung startet die interne
Engine nicht; eine hinzukommende Steuerung beendet die lokale Suche.

## Bewertungsanzeige

Spielende (Ergaenzung vom 20. November): Weiss gewinnt = E4/D5,
Schwarz gewinnt = D4/E5, Remis = D4/E4/D5/E5. Die Anzeige hat Vorrang vor
Bewertung und Zugkorrekturen und bleibt auch beim Abräumen eingeschaltet.
Erst die 400 ms stabil erkannte vollstaendige Grundstellung beendet das
Spiel und loescht die LEDs. Danach braucht ein neues Spiel wieder F4.
Die Gewinnfarbe ist unabhaengig von der gewaehlten Menschenfarbe.

Nach jedem bestaetigten Zug eine Sekunde Bewertung aus Sicht des Menschen:

| Bewertung | Felder |
| --- | --- |
| weniger als ein Bauer Vorteil/Nachteil | D1 und E1 |
| -1 / -2 / -3 oder schlechter | C1 / B1 / A1 |
| +1 / +2 / +3 oder besser | F1 / G1 / H1 |

Plus bedeutet immer Vorteil Mensch, auch wenn er Schwarz spielt. Verwendet
wird Virgins statische Stellungsbewertung nach dem ausgefuehrten Zug.
Figurenkorrekturen haben Vorrang. Nach einer Sekunde folgen wieder die
normalen Zug-/Korrekturhinweise. ChessLink verwendet die untere Eck-LED-Reihe;
D1 und E1 teilen sich dort eine LED und ergeben drei physische Lichtpunkte.
Cynus zeigt die Bewertung als Zahl, da es keine Brett-LEDs hat.

## Isolation und Sicherung

- `src/v14_stable/`: unveraenderlicher Quell-/Headerabschluss mit FREEZE.json.
- `src/xmasgift/`: eigene Kopie, eigene Engine, eigene Spiel-/Transportlogik.
- Build-Ziele `V14_stable` und `eChessGW_XMasGift` erben keine aktiven
  Entwicklungsquellen. Bestehende Builds schliessen beide Ordner aus.
- `backups/v14-stable-20261119/`: bisheriges Image, ELF, Map, Bootloader,
  Partitionen und Quellstand vor XmasGift. Firmware-SHA256:
  `0cb24a0d27115d4c4c0e64fad1e95d5f2742806f9a81156de34beb6f7187d12c`.
- Das V14-Webpaket bewahrt genau diese bisherigen Firmwarebytes. Der eigene
  V14-Neubau dient als Nachweis, dass der eingefrorene Abschluss selbst baut;
  eingebettete Zeitstempel/Pfade koennen andere binaere Bytes erzeugen.
- Die bisherigen Machine-/Promotion-Routinen sind in XmasGift bytegleich mit
  dem Freeze; neue Eingriffe liegen in eigenem Runtime-Modul und vier lokalen
  Adapterkopien (main, board_driver, certabo_board, cynus_board).
- Der bisherige BT-Verbindungs-/Einlernablauf bleibt erhalten. Xmas-Polling
  erfolgt danach; ab der Gestenwahl darf F4 direkt Virgin starten, ohne erst
  A4/B4 zu verlangen. Waehrend lokaler Virgin-Partien ist die optionale
  Maskierungswahl pausiert, damit zusaetzliche Damen keine Hostwahl ausloesen.
  Eine fehlgeschlagene Task-Anlage laesst die Board-Suche nicht dauerhaft
  auf "beschaeftigt" stehen. Synthetische Offline-Tests pausieren neue
  Board-Suchaufgaben waehrend der lokalen Suche und geben sie danach frei.

## C3-Speicherprofil

Virgin Fire/Adaptive, ein Suchthread, 2500 ms, fuenf Kandidaten, 20-Ply-Grenze.
Die C3-Anpassungen aendern keine Schachregeln oder Adaptive-Auswahlregeln:
begrenzter exakter Countermove-Cache, kompakte Reduktionstabelle, Wiederverwendung
bereits abgearbeiteter Zuglistenplaetze und 8-KiB-Hashtabelle.

56 KiB Suchstack; Session-Suchspeicher und Hash werden pro Zug angelegt und
nach Suchende freigegeben. Der Start prueft freien internen Heap und groessten
Block. Die Hash-Allokation meldet Fehler ohne Exception. Ein erster Versuch
mit konservativen 72 KiB Stack passte nicht; bei einem zweiten Versuch mit
16 KiB Hash fuehrte Fragmentierung beim erneuten Start zu einer fehlgeschlagenen
Allokation. Die veroeffentlichte Variante verwendet den korrigierten 8-KiB-Pfad.
Private Diagnoseprotokolle liegen unter `output/xmasgift-20261119/`.

## Pruefung und Grenzen

- V14-Stable-Freeze und XmasGift bauen mit Espressif32 7.0.1,
  Arduino 2.0.17 und NimBLE-Arduino 2.5.1.
- Native Tests: beide Gesten/Farben, vollstaendige Grundstellung, 2+2-Signal,
  Entfernen der Zusatzdame waehrend des Signals, Startschutz, Anheben/Zurueckstellen,
  Abbruch/erneute Geste, Bewertungszeit/Vorrang, Rochade, en passant,
  Unterverwandlung und Virgins Suchregressionen bestanden.
- Bisherige Gateway-, VirtualOperator-, physische und virtuelle
  Umwandlungstests bestanden. Freeze- und Isolation-Pruefsummen kontrolliert.
- Auf dem echten C3: zwei Spiele mit je vier Halbzuegen und beiden
  Menschenfarben, echte Virgin-Suchen, anschliessend Suchabbruch beim
  Wiederaufbau der Grundstellung. Fuenf Kandidaten, Tiefe 3-4,
  ca. 2.5-2.7 Sekunden pro Suche; mindestens 20 KiB gemessene Stackreserve
  in diesen Suchstellungen. Nach Ende: keine aktive Suche, kein Spiel,
  Suchspeicher freigegeben, Heap wieder verfuegbar.
- Diese automatischen Geraetetests verwenden **synthetische Brettmeldungen**,
  keine echte BLE-Brettverbindung. Physische F4-Gesten, LED-Erscheinungsbild,
  BLE-Last mit verbundenem Brett und Roboterausfuehrung brauchen noch einen
  Benutzertest. Tiefe/Stackwerte sind keine Garantie fuer alle Stellungen.
- Der erste physische Certabo-Versuch meldete fehlende Maskierungswerbung.
  Die fruehe Xmas-Einbindung und der Scan-Task-Fehler wurden daraufhin
  korrigiert. Die korrigierte Gesamtfirmware wurde erneut geflasht; ihre
  laufende Brettsuche ist im Live-Log bestaetigt. Die Such-/Spieltests
  betreffen denselben unveraenderten Virgin-/Spielkern vor dieser abschliessenden
  BT-Anpassung. Nach Neustart der Certabo-Gegenstelle ist die Brettverbindung
  im Live-Log bestaetigt: Grundstellung und e2-e4 werden korrekt gemeldet.
  Die Gegenstelle ist eine App, die ein virtuelles Certabo-Brett bereitstellt;
  diese BLE-Brettverbindung muss fuer den Test bestehen bleiben und ist nicht
  mit einem externen Engine-/Spielclient am Maskierungsserver gleichzusetzen.
  Auch die Zusatzdame auf F4 wurde in einer Brettmeldung korrekt decodiert.
  Der Benutzer hat klargestellt: F4 ist eine Alternative zur Maskierungswahl.
  Die Freigabe wurde entsprechend vor die optionale A4/B4-Wahl verschoben;
  die Korrektur wurde gebaut und auf COM9 geflasht (Hashpruefung erfolgreich).
- Live-Test mit verbundenem virtuellem Certabo nach dieser Korrektur:
  F4 weiss startet Virgin direkt, ohne Maskierung. Das Protokoll bestaetigt
  Mitte zweimal und Ecken zweimal mit je 250 ms. Danach 1.e4 d5 2.exd5,
  jeweils bestaetigte Halbzuege und Bewertungs-LEDs (D1/E1, spaeter F1).
  Virgin antwortete auf e4 mit d7d5, Tiefe 3, fuenf Kandidaten, 3923 Knoten,
  2551 ms und 28824 Bytes gemessener Stackreserve. Der Test betrifft ein
  virtuelles Certabo; andere physische Brettmodelle sind damit nicht abgenommen.

Explizite USB-Diagnose ohne physisches Brett/externen Host:
`?xmas status`, `?xmas test white`, `?xmas test black`, `?xmas test cancel`.
Testmodus sendet keine Brett-/Motorbefehle und bricht bei realer Verbindung ab.
Das `?`-Praefix laesst die `#`-Bestaetigungen des bisherigen PGN-Tools unangetastet.

Tests: `tools/test_xmasgift.cmd`, `tools/test_xmasgift_device.py`.
Originale Anforderung: `docs/virgin_weihnachtsgimmick_20261119.md`.

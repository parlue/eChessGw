# Physische Umwandlung im Engine-Duell ? 2026-11-08

## Verhalten
Die ziehende Engine bekommt vor der Roboterbewegung gueltige ChessLink-
Stellungsantworten fuer Anheben, gegebenenfalls Entfernen des Schlagopfers
und moegliche Umwandlungsfiguren. Die Rueckmeldung ueber L/X und frische
Statusabfragen bestimmt die akzeptierte Figur. Die Kandidaten werden derzeit
in der Reihenfolge N, B, R, Q geprueft; keine davon ist eine Voreinstellung.
Die Pruefstellungen gehen nur an die ziehende Engine, nicht an das Gegenueber,
den Roboter oder den PGN-Recorder. Es wird kein neues Drahttelegramm erfunden.

Nach vollstaendig erfolgter Auslieferung sind frisches LED-Aus/X, weitere
Hostaktivitaet und ein zusammenhaengendes ruhiges Zeitfenster erforderlich.
Blinkphasen, vorheriges Clear, Schweigen und Antworten der falschen Engine
bestaetigen nichts. Die Blinkperiode wird beruecksichtigt. Ein unbestaetigter
Kandidat wird zuerst wieder vom Ziel genommen, bevor der naechste gemeldet
wird. Keine Antwort, Ablehnung aller Figuren oder veraenderte Brettstellung
stoppen den Ablauf. Verbindungsverlust setzt die Beobachtungsnachweise
zurueck; nach Wiederverbindung sind neue Nachweise erforderlich.

Nach Bestaetigung:
- Cynus bekommt genau einen Auftrag mit Suffix, z.B. move a7a8q.
  Das Format entspricht dem bestehenden CynusTactics-App-Pfad
  MainActivity.kt/cynusReplyToArm. Ein fehlgeschlagener Motor-Schreibversuch
  wird nicht automatisch wiederholt und nicht als ausgefuehrt gespeichert.
- Chessnut Move bekommt genau einen vollstaendigen Zielstellungsauftrag
  mit der bestaetigten Figur.
- Erst die tatsaechlich passende Brettstellung startet den bestaetigten
  Replay fuer beide Hosts. Eine falsche Figur bestaetigt den Zug nicht.
  Festschreibung und PGN erfolgen genau einmal mit der gewaehlten Figur.

## Code
Neue Dateien include/machine/physical_promotion.h und
physical_promotion_runtime.inc. Legalitaetsplaene und bestehende
Bestaetigungsstruktur werden mit dem virtuellen Pfad geteilt, ohne diesen
im Standardbuild einzuschalten. Standard-Kabel-/Standalone-Pfade und
gefrorene Quell-Snapshots bleiben unangetastet.

## Pruefung
Vier Suiten bestanden, Logs unter .pio/*_verification_20261108.log:
- test_physical_promotion.cmd: 114 Faelle einschliesslich der 4 Resetfaelle
  ohne eigene Ausgabe; 64 erfolgreiche Kombinationen aus Bretttyp, Farbe,
  Schnittstelle, Schlagen und Figur. Der produktive Cynus-Befehl wird im
  Test kompiliert und das genaue UCI-Kommando geprueft. Transport/Sensoren
  sind simuliert. Weitere Faelle: Ablehnung, langsames/schnelles Blinken,
  Schweigen, altes Clear, X-Bestaetigung, falscher Host, Reset,
  Brett-/Abo-Wiederverbindung, Schreibfehler, veraenderte Brettstellung.
- test_machine_gateway.cmd: normale Zuege, Schlagen, en passant,
  beide Rochaden beider Farben mit produktivem Cynus-Decoder, M5-Einmalzug,
  Boardauswahl, Reset und weitere bestehende Regressionen.
- test_virtual_operator.cmd: bestehende virtuelle/physische Regressionen.
- test_chessnutmove.cmd: produktiver Move-Treiber gegen BLE-/Sensordoubles,
  volle Motorpakete, Sonderzuege, alle Umwandlungsfiguren und Fehlerfaelle.

Es gibt weiterhin die bekannten vswhere-Suchwarnungen und zwei native
Konvertierungswarnungen. Der ESP32-Produktionsbuild ist erfolgreich.
Die Tests ersetzen keinen realen Durchlauf mit Phoenix und Roboter. Die
Bestaetigung ueber LED-Rueckmeldung ist die hier implementierte
Verhaltensauswertung, kein zusaetzlicher expliziter Umwandlungs-ACK der
veroeffentlichten Protokollbeschreibung.

## Flash
Nach ausdruecklichem Auftrag geflasht: esp32-c3-supermini, COM4,
ESP32-C3, MAC e0:72:a1:23:5e:e0. Alle Segmente von esptool hash-verifiziert,
RTS-Neustart ausgeloest. Firmware 798640 Bytes; RAM 80836 Bytes,
Code/Flash 745814 Bytes. Die Beta enthaelt auch die zuvor noch nicht
geflashte en-passant-Korrektur sowie Rochade und Grundstellungs-Brettsuche.

Firmware SHA256: 26863b2ad685ce30ff51c9a35d64eb96fbc2fb00fd6d0b65fabfc3e564226039

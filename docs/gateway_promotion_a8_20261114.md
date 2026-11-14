# Gateway: virtuelle Umwandlung Phoenix a7-a8

## Beobachtung

Live am 14.11.2026 auf COM9 (USB-Seriennummer 70:AF:09:0D:A8:24)
passiv mitgelesen, ohne Reset oder gesendete Bytes. Phoenix spielt Weiss,
Engine gegen Engine ohne E-Board, unser ESP ersetzt Bauerino.

Das Log zeigt wiederholt `no-unique-move matches=4 fields=a7 a8`.
`lifting=0`, kein Kandidat und weiter Weiss am Zug. Der Zug wird schon
vor der Ausfuehrung verworfen: Der Standardpfad zaehlt die vier legalen
Umwandlungsfiguren als vier verschiedene Zuege. Laut Anwender will Phoenix
auf a8 eine weisse Dame. Die LED-Endpunkte enthalten die Figur nicht.

## Korrektur

Die vorhandene virtuelle Figurenverhandlung wird auch im Standardbuild
kompiliert. Die Erkennung zaehlt eindeutige Start-/Zielfeldpaare; eine
Umwandlung startet ausschliesslich fuer die ziehende Engine die vorhandenen
Figurenversuche. Erst nach deren Bestaetigung wird die ausgewaehlte Figur
an die andere Engine weitergegeben und die Stellung einmal verbucht.
Keine automatische Damenannahme, Unterverwandlungen bleiben moeglich.
Normale virtuelle Zuege behalten ihren bisherigen Ausfuehrungspfad.
Der vollstaendige VirtualOperator bleibt als eigenes Beta-Environment erhalten.

Die SDK-Plattform ist auf das bisher fuer diesen Gateway verwendete
`espressif32@7.0.1` festgelegt. Der unversionierte Name hatte inzwischen
die fuer andere ESP-Projekte installierte pioarduino-Plattform ausgewaehlt.

## Pruefung

Bestanden:

- `tools/test_virtual_promotion_default.cmd`: 34 Faelle, Standardbuild ohne
  VirtualOperator-Beta-Flag, beide Farben und Hosts, Q/R/B/N, mit/ohne
  Schlagen und Ablehnung aller Figuren. Weiss prueft jetzt a7-a8 bzw. a7xb8.
- `tools/test_machine_gateway.cmd`: bisherige Standard-Gateway-Regression.
- `tools/test_virtual_operator.cmd`: vollstaendiger virtueller Beta-Pfad.
- `tools/test_physical_promotion.cmd`: physische Figurenverhandlung.

Native Tests verwenden Transport-Doubles. Ein echter Phoenix-Test nach dem
Aufspielen ist weiterhin erforderlich; die laufende Partie wurde nicht
unterbrochen. Vorherige Quellen, Firmware und Test-/Buildprotokolle liegen
unter `output/promotion-a8-20261114/`; das passive Live-Log liegt unter
`output/phoenix-m5-stall-20261102/promotion-a8-20261114.txt`.

Firmware-Build: esp32-c3-supermini mit SDK 7.0.1 erfolgreich. Flash 750612 Byte, RAM 82204 Byte. Neues Image: output/promotion-a8-20261114/firmware-promotion-fix.bin. Noch nicht aufgespielt.

Aufgespielt am 14.11.2026 auf COM9 (70:AF:09:0D:A8:24), esptool-Hash verifiziert. Anschliessend BLE-Suche im passiven Log beobachtet. Echter Phoenix-Umwandlungstest weiterhin offen.

# Spectrum-Abgleich mit eChessTactics (24.10.2026)

Entwicklungsstand esp32-c3-supermini. Frozen-Versionen, andere Bretttreiber und
King/Phoenix-Kabelprotokoll bleiben unveraendert.

Erkennung: BLE-Namen Certabo oder Spectrum, danach NUS-Servicepruefung.
Mindestens acht IDs mit 2,0 und keine 93/94 erkennen Spectrum; mindestens acht
93/94 und keine 2,0 erkennen Mono-Certabo. Bis Neuverbindung fest; Vorrang vor L/D.
Erst nach vollstaendigem Stellungsframe werden LEDs gesendet.

Fallback auf Nutzerwunsch: drei originale BLE-Notifications mit exakt 64 Byte
und ':' am Anfang plus drei vollstaendige Stellungsframes, ohne bisherige 93/94.
Spaetere eindeutige Tag-Klassifikation hat Vorrang. Das ist die in CynusClient
protokollierte Paketgroesse, NICHT die ausgehandelte MTU! Interne RX-Queue-Stuecke
werden nicht als Notifications gezaehlt. L/D bleibt sonst als bisheriger Hinweis.
Diese Merkmale stammen aus getesteten Chips/Firmware, keine universelle Garantie.

Nur tag-bestaetigtes Spectrum benutzt den App-Rauschfilter: echte 2,0-Tags und
Nullwerte passieren sofort; andere Werte sind Rauschen. Vorheriger Tag wird
maximal drei verrauschte Scans gehalten, danach leer. Unbekannte echte Tags und
Duplikate werden weiter verworfen. Paket-Fallback allein aktiviert diesen
praefixabhaengigen Filter nicht.

34 Figuren bei jeder Verbindung neu einlernen: Grundstellung plus weisse Dame
d3, schwarze Dame d6. Archivierte IDs werden nie fuer die Live-Kalibrierung geladen.
Spectrum bleibt wie die App waehrend der 4,2-s-Bestaetigungsphase dunkel; Mono
behaelt sein Blinken. Danach Abweichungen/Zusatzdamen anzeigen, nur Grundstellung
freigeben. Unveraenderte Spectrum-LEDs nur alle 10 s erneuern; Mono unveraendert.

RGB: 247 Bytes (FF55, 81 RGB-Tripel, CRLF), vier Ecken je Feld, kein FF55 im Payload.
Default Rot 80 % (204/255). Gesten nach dem Einlernen: normale Grundstellung plus
genau eine eingelesene weisse Zusatzdame, eine Sekunde ruhig aufstellen:

| Feld | Einstellung |
|---|---|
| g3 | Rot |
| g4 | Blau |
| g5 | Gruen |
| g6 | Orange |
| h3 | 25 % |
| h4 | 50 % |
| h5 | 75 % |
| h6 | 100 % |

Anschliessend Dame entfernen. Konfigurationsstellung wird nicht als Spielzug an
den Host gesendet. Bestaetigung: d4/e4/d5/e5 blinken dreimal. NVS spectrum:
color/bright. Gleiche gehaltene Geste speichert nur einmal; ungueltige oder
veraltete Scans verhindern die ausstehende Uebernahme. Andere Bretter unveraendert.

Tests: tools/test_spectrum_alignment.cmd. Build: pio run -e esp32-c3-supermini.
Hardware noch pruefen: Erkennung, Fallback, Kalibrierung, alle LEDs aus, Eckmapping,
Gesten samt Wiederverbindung, King/Phoenix-Zuege, Mono-Regression.
Kein Flash, Freeze oder Webflasher-Update durch diesen lokalen Abgleich.

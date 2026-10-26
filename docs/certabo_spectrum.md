ï»¿# Spectrum-Abgleich mit eChessTactics (24.10.2026)

Entwicklungsstand esp32-c3-supermini. Frozen-Versionen, andere Bretttreiber und
King/Phoenix-Kabelprotokoll bleiben unveraendert.

## Gateway-Erkennung (26.10.2026)

In eChessTactics wird Spectrum explizit ausgewaehlt. Automatische Erkennung
betrifft hier ausschliesslich das Gateway. Figuren koennen auf beiden Brettern
verwendet werden; kein ID-Byte, Praefix oder gespeichertes Figurenarchiv darf
ueber den LED-Treiber entscheiden.

BLE-Namen Certabo oder Spectrum finden Kandidaten; danach NUS-Servicepruefung.
Die MTU-Anforderung bleibt unveraendert 256. Ausgewertet werden ausschliesslich
originale BLE-Notifications und eigenstaendige Protokollmarker:

- D: positiver RGB-Hinweis; L allein unterscheidet die Module nicht, da auch
  Spectrum L senden kann. Buchstaben innerhalb von Stellungen/Diagnosetext
  werden nicht als Marker gelesen.
- Ohne D: mindestens drei vollstaendige Stellungen und drei originale
  Positionsstart-Notifications (erstes Byte ':'). Exakt 64 Byte kennzeichnet
  das beobachtete Spectrum-Modul; mehr als 64 Byte das klassische Modul.
- Widerspruechliche Groessenprofile oder D zusammen mit breitem Profil sperren
  LED-Ausgaben. Unbekannte Profile bleiben unentschieden, ohne Zeit-Default.
- Auch bei D wird vor der ersten vollstaendigen Stellung nichts geschrieben.

Die drei letzten vollstaendigen Anfangsstellungen werden begrenzt im RAM
gepuffert und nach der Entscheidung in Reihenfolge ans Einlernen uebergeben.
Bei laenger unentscheidbarem Stream werden aeltere Frames ersetzt (960 Byte
Nutzdaten, kein unbegrenzter Puffer). Queue-Verlust verwirft Typ-Evidenz und
Puffer; defekte Frames verwerfen gepufferte Kalibrierungssamples.
Interne 128-Byte-Queue-Stuecke gelten niemals als eigene BLE-Notifications.

**Grenze:** Die Paketgroessen sind Fingerabdruecke der beobachteten Module,
kein universell zugesichertes Modellkennzeichen. Senden andere Firmwarestaende
beider Module identische Marker und Paketgroessen, ist eine sichere automatische
Unterscheidung daraus nicht moeglich; dann wird ein explizites Modellmerkmal
oder eine Gateway-Auswahl benoetigt. Hardwarepruefung beider Module steht aus.

Der alte Spectrum-Rauschfilter mit festem 2,0-Praefix ist entfernt. Wie beim
Certabo gelten die sieben Kalibrierungssamples und danach die eingelernten IDs:
Unbekannte oder doppelte IDs halten die gesamte Stellung zurueck und werden
niemals in leere Felder verwandelt. Das unterstuetzt dieselben Figuren auf
beiden Brettern ohne falsche Bretttyp-Zuordnung.

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
Hardware noch pruefen: Erkennung beider Transportprofile, Kalibrierung, alle LEDs aus, Eckmapping,
Gesten samt Wiederverbindung, King/Phoenix-Zuege, Mono-Regression.
Kein Flash, Freeze oder Webflasher-Update durch diesen lokalen Abgleich.

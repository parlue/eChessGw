# Virgin Adaptive: festgelegter Ablauf

Abgestimmte Anforderungen vom 19. November 2026. Dies ist die Spezifikation;
eine fertige Gateway-Spielintegration oder Hardwareabnahme ist damit nicht behauptet.

## Aktivierung

Nur im eigenstaendigen Betrieb ohne Kabelcomputer, mit verbundenem und
vollstaendig eingelesenem physischem Brett. Certabo/Spectrum durchlaufen zuerst
das bisherige Einlernen der 34 Figuren und Entfernen der Zusatzdamen.
Der bisherige BT-Verbindungs- und Einlernvorgang muss fuer jedes Brett
abgeschlossen sein. Danach stehen alternative Gesten zur Wahl: A4/B4 fuer
eine Maskierung oder F4 fuer Virgin direkt. Fuer Virgin ist keine Maskierung
erforderlich (Benutzerklarstellung). F4 darf Verbindung und Einlernen nicht
abfangen oder deren Zustand zuruecksetzen. Eine virtuelle Certabo-App bleibt
als Brett-Gegenstelle verbunden.

Die neue Geste besteht aus der vollstaendigen normalen Grundstellung und
genau einer zusaetzlichen Dame auf **F4**. Keine Stammfigur wird versetzt.

| Zusatzfigur auf F4 | Mensch | Virgin | Erster Zug |
| --- | --- | --- | --- |
| Zweite weisse Dame | Weiss | Schwarz | Mensch |
| Zweite schwarze Dame | Schwarz | Weiss | Virgin |

Erkennung entprellen und pro Aufstellen nur einmal ausloesen. Beide Damen
gleichzeitig oder andere Abweichungen von der Grundstellung sind keine Startgeste.
Aktive Kabelsteuerung oder ein externer steuernder BLE-Client darf nicht
gleichzeitig mit der internen Engine das Brett steuern.

## Rueckmeldung und Startschutz

Nach erfolgreicher Vorbereitung von Virgin Adaptive:

1. Die vier Mittelfelder D4, E4, D5, E5 blinken **zweimal**.
2. Anschliessend blinken die vier Eckfelder A1, H1, A8, H8 **zweimal**.
3. Zusatzdame entfernen. Erst wenn das Signal abgeschlossen und die normale
   Grundstellung vollstaendig erkannt ist, beginnt das Spiel.
4. Mensch-Weiss: auf den ersten Menschenzug warten. Mensch-Schwarz: Virgin
   berechnet den ersten weissen Zug und zeigt seine Ausfuehrung an.

Wichtig: Die Grundstellung in Schritt 3 ist der START, kein Abbruch. Auch
wiederholte gleiche Brettmeldungen waehrend Virgins erster Suche brechen
nichts ab. Erst ein bestaetigter legaler Zug aktiviert die Erkennung fuer
die spaetere Rueckkehr zur Grundstellung. Ein blosses Anheben und
Zurueckstellen einer Figur zaehlt nicht als ausgefuehrter Zug.

Die zusaetzliche Dame ist ein Bedienzeichen, kein Spielzug und kein Teil
der Suchstellung. Die Suchstellung beginnt mit der normalen Grundstellung.

## Ende und Neustart

Eine spaetere vollstaendige, stabil erkannte Grundstellung beendet die Partie
und stoppt eine laufende Suche. Suchspeicher erst nach bestaetigtem Ende des
Suchworkers freigeben. Veraltete Suchergebnisse duerfen keine neue Partie erreichen.
Die Grundstellung startet keine neue Partie: erneut eine der F4-Gesten verwenden.
Normale Maskierung/Gesten stehen anschliessend wieder zur Verfuegung.

## Adaptive und Bewertung

Virgin Adaptive nach dem bestehenden kompakten cer4all-Ansatz. Vorhandene
C3-Machbarkeitswerte: `.c3-virgin-check/RESULT.json` (2. November 2026).
Gemeinsamer Build gelang; freie Laufzeitreserven bei aktiver BLE-Verbindung
und ausreichende Stackreserve sind noch auf Hardware zu messen.

Nach jedem vollstaendig ausgefuehrten Zug eine Sekunde Bewertung auf Reihe 1:

- Immer aus Sicht des MENSCHEN: rechts/Plus ist Vorteil Mensch,
  links/Minus ist Vorteil Virgin, unabhaengig von der Figurenfarbe.
- D1/E1 sind die ausgeglichene Mitte; pro Bauernwert etwa ein Feld weiter
  nach links oder rechts; am Rand begrenzen.
- Nach Ablauf wieder normale Zug-/Korrekturhinweise anzeigen.
- Auf ChessLink-Brettern nur die unteren Feld-LEDs verwenden.
  Gewuenscht sind vier LEDs fuer D1/E1. Die tatsaechliche Eck-LED-Geometrie
  muss geprueft werden: benachbarte Felder koennen sich eine LED teilen.
- Schwelle fuer "nahe ausgeglichen", Rundung und Blinkzeiten sind noch
  Implementierungsdetails; bislang wurden keine konkreten Zahlen vereinbart.

## Vorhandene Gesten beruecksichtigen

- Weisse Zusatzdame A4: ChessLink-Maskierung; B4: Chessnut-Maskierung.
- Weisse Zusatzdame C4: USB-PGN-Ausgabe.
- G3-G6/H3-H6: LED-Farbe/Helligkeit bei Spectrum/iChessOne.
- Zusatzdamen D3/D6: Certabo-Einlernen; E3/E6: Spectrum-Einlernen.
- Cynus nutzt den schwarzen Koenig auf B5/B6, C5/C6, D5/D6, E5/E6
  fuer Optionen. PGN-Ergebnissignale verwenden beide Koenige im Zentrum.
- F4 mit Zusatzdame ist in den geprueften bestehenden Gesten nicht belegt.

## Notwendige Abnahme

Beide Startfarben, genaue Blinkfolge, Entfernen der Zusatzdame vor/waehrend/
nach dem Signal, wiederholte Grundstellungsmeldungen bei der ersten Suche,
Anheben/Zurueckstellen, spaeterer Abbruch waehrend Suche und Zugausfuehrung,
Neustart per neuer Geste, falsche/unsichere Scans, BLE-Verlust, externe
Steuerung sowie bestehende Maskierungs-, Einlern- und Kabelablaeufe pruefen.
Bewertungsrichtung fuer beide Menschenfarben und Wiederherstellung der
Zughinweise nach einer Sekunde pruefen.

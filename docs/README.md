# eChessGw Web Flasher

The normal installer uses `manifest.json`, stable release **V12** for ESP32-C3 SuperMini.
The tester-confirmed Certabo/Spectrum firmware is promoted byte-for-byte; there is no beta installer.

- Certabo: normal starting position plus white extra queen d3 and black extra queen d6.
- Spectrum: normal starting position plus white extra queen e3 and black extra queen e6.
- Wait for all 34 chips to be learned and three centre flashes, then remove the extra queens when indicated.
- Start play only after the correct normal starting position is recognized.

V12 files, matching source and hashes are in `releases/v12/`. V11 is retained in `releases/v11/`;
old beta folders are historical archives and are no longer offered by the installer.

GitHub Pages: main branch, /docs. Installer: https://parlue.github.io/eChessGw/

# eChessGw Web Flasher

The normal installer continues to use `manifest.json` (stable v11).
The separate expandable Spectrum beta installer uses
`beta/spectrum-20261015/manifest.json`, with its own binaries and corresponding
source archive. Target: ESP32-C3 SuperMini. Hardware confirmation is pending;
freeze the development version only after the tester confirms operation.

The manifest selects the complete ESP32-C3 image set: bootloader, partition
table, boot application selector and the eChessGw gateway firmware.

This `docs` directory is served from the root of the eChessGw GitHub
repository via GitHub Pages.

To (re-)enable it: Settings -> Pages, then select:

- Source: Deploy from a branch
- Branch: main
- Folder: /docs

Once GitHub Pages has deployed, the installer is available at:

https://parlue.github.io/eChessGw/

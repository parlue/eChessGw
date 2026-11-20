"""Freeze the current SuperMini and create an isolated Christmas variant once."""
from pathlib import Path
import hashlib, json, shutil

root = Path(__file__).resolve().parents[1]
freeze = root / 'src/v14_stable'
gift = root / 'src/xmasgift'
saved = root / 'backups/v14-stable-20261119'
assert not freeze.exists() and not gift.exists() and not saved.exists()
saved.mkdir(parents=True)
build = root / '.pio/build/esp32-c3-supermini'
expected = '0cb24a0d27115d4c4c0e64fad1e95d5f2742806f9a81156de34beb6f7187d12c'
assert hashlib.sha256((build/'firmware.bin').read_bytes()).hexdigest() == expected
for name in ('firmware.bin','firmware.elf','firmware.map','bootloader.bin','partitions.bin'):
    if (build/name).exists(): shutil.copy2(build/name,saved/name)
shutil.copy2(root/'platformio.ini',saved/'platformio.before.ini')
freeze.mkdir()
for obj in (build/'src').glob('*.cpp.o'):
    shutil.copy2(root/'src'/obj.name[:-2],freeze/obj.name[:-2])
for p in (root/'include').rglob('*'):
    if p.is_file() and not any(x.startswith('machine_v') for x in p.parts) and '_v' not in p.name:
        q=freeze/p.relative_to(root/'include');q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
hashes = {str(p.relative_to(freeze)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest()
    for p in freeze.rglob('*') if p.is_file()}
(freeze/'FREEZE.json').write_text(json.dumps(dict(version='V14_stable', firmware_sha256=expected,
    provenance='Current SuperMini source; exact firmware flashed 2026-11-14. No Xmas code.',files=hashes),indent=2)+'\n')
shutil.copytree(freeze,saved/'source')
shutil.copytree(freeze,gift)
(gift/'FREEZE.json').unlink()
shutil.copytree(root/'.cer4all-beta/work/virgin',gift/'virgin')
ini=root/'platformio.ini';s=ini.read_text()
s=s.replace('    -<v13_move/>','    -<v13_move/>\n    -<v14_stable/>\n    -<xmasgift/>',1)
config='''

; Independent V14 freeze. Never inherits active development or Xmas settings.
[env:V14_stable]
platform = espressif32@7.0.1
board = esp32-c3-devkitm-1
framework = arduino
monitor_speed = 115200
upload_speed = 115200
build_src_filter = -<*> +<v14_stable/>
build_flags =
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=1
    -D MILLENNIUM_BAUD=38400UL
    -D CHESSLINK_ENABLE_MACHINE=1
    -D CHESSLINK_ENABLE_CERTABO=1
lib_deps = h2zero/NimBLE-Arduino@2.5.1
platform_packages =
    framework-arduinoespressif32 @ 3.20017.241212+sha.dcc1105b
    toolchain-riscv32-esp @ 8.4.0+2021r2-patch5
    tool-esptoolpy @ 2.41100.0

[env:eChessGW_XMasGift]
extends = env:V14_stable
build_src_filter = -<*> +<xmasgift/>
build_unflags = -std=gnu++11
build_flags =
    ${env:V14_stable.build_flags}
    -std=gnu++17
    -Isrc/xmasgift/virgin
    -DVIRGIN_EMBEDDED=1
    -DVIRGIN_EMBEDDED_HASH_KIB=16
    -DVIRGIN_EMBEDDED_MAX_PLY=20
'''
ini.write_text(s+config,encoding='utf-8')
print('V14_stable saved:',saved,'files:',len(hashes),'firmware:',expected)

"""Publish-ready packages for the V14 freeze and isolated Christmas variant."""
from pathlib import Path
import hashlib,json,re,shutil,zipfile

root=Path(__file__).resolve().parents[1]
repo=root/'experiments/webflasher-v13-move'
docs=repo/'docs'
saved=root/'backups/v14-stable-20261119'
freeze=root/'src/v14_stable'
gift=root/'src/xmasgift'
build=root/'.pio/build/eChessGW_XMasGift'
stable=docs/'releases/V14_stable'
release=docs/'beta/eChessGW_XMasGift'
assert not stable.exists() and not release.exists(),'Never overwrite released packages'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
protected=[p for base in (docs/'releases',docs/'beta') for p in base.rglob('*') if p.is_file()]
before={str(p):sha(p) for p in protected}
meta=json.loads((freeze/'FREEZE.json').read_text())
assert all(sha(freeze/n)==h for n,h in meta['files'].items())
assert sha(saved/'firmware.bin')==meta['firmware_sha256']
assert (build/'firmware.bin').stat().st_size<=1310720
device=(root/'output/xmasgift-20261119/device-search-test.txt').read_text()
for mode in (1,2,3):assert '[XMAS TEST] PASS mode='+str(mode) in device
assert 'abort()' not in device and 'Guru Meditation' not in device
for folder in (stable,release):folder.mkdir(parents=True)
for n in ('firmware.bin','bootloader.bin','partitions.bin'):shutil.copy2(saved/n,stable/n)
for n in ('firmware.bin','bootloader.bin','partitions.bin'):shutil.copy2(build/n,release/n)
for folder in (stable,release):shutil.copy2(docs/'boot_app0.bin',folder/'boot_app0.bin')
assert (stable/'partitions.bin').read_bytes()==(release/'partitions.bin').read_bytes()
assert (stable/'bootloader.bin').read_bytes()==(release/'bootloader.bin').read_bytes()
manifest=json.loads((docs/'manifest.json').read_text())
for folder,name,version in ((stable,'eChessGw V14_stable','V14_stable'),
        (release,'eChessGW_XMasGift','eChessGW_XMasGift-0.8.7-20261120')):
    m=json.loads(json.dumps(manifest));m['name']=name;m['version']=version
    for part in m['builds'][0]['parts']:
        if part['offset']==65536:part['path']='firmware.bin'
    (folder/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
    for part in m['builds'][0]['parts']:assert (folder/part['path']).exists()
(stable/'README.md').write_text('''# V14_stable - 19 November 2026

Saved current ESP32-C3 SuperMini gateway, before the Christmas feature.
Includes the current virtual-promotion negotiation in the standard build.
This package preserves the exact 803664-byte firmware already flashed on
14 November. Its original embedded build timestamp is retained.

No Virgin engine or Christmas gesture is compiled into this version.
The independent source/header closure is in src/v14_stable; build V14_stable.
The source closure also rebuilt successfully with Espressif32 7.0.1,
Arduino 2.0.17 and NimBLE-Arduino 2.5.1. Rebuilding can differ due to paths
and timestamps; published firmware bytes are kept unchanged.

The standard gateway, VirtualOperator, physical-promotion and default
virtual-promotion test suites passed. Their transport doubles do not prove
every physical board combination. No new hardware certification is implied
by saving the current version under the requested V14_stable name.

Previous V13 Move, V12 and historical beta packages are retained unchanged.
source.zip contains the standalone frozen build. FREEZE.json records source
and original firmware hashes; SHA256.json covers this package.
''',encoding='utf-8',newline='\n')
shutil.copy2(freeze/'FREEZE.json',stable/'FREEZE.json')
shutil.copy2(root/'docs/xmasgift_release_20261119.md',release/'README.md')
provenance=dict(version='eChessGW_XMasGift',engine='Virgin 0.8.7 Beta',date='2026-11-20',
    firmware_sha256=sha(build/'firmware.bin'),firmware_bytes=(build/'firmware.bin').stat().st_size,
    base_firmware_sha256=meta['firmware_sha256'],
    physical_board_test='Chessnut and virtual Certabo confirmed on previous engine; 0.8.7 board gameplay pending',
    device_tests='See README for dated C3 synthetic diagnostics and their engine version; not a physical-board certification',
    files={p.relative_to(gift).as_posix():sha(p) for p in sorted(gift.rglob('*')) if p.is_file()})
(release/'PROVENANCE.json').write_text(json.dumps(provenance,indent=2)+'\n')
config=(root/'platformio.ini').read_text()
base=config.split('[env:V14_stable]',1)[1].split('[env:eChessGW_XMasGift]',1)[0].strip()
stable_ini='[platformio]\ndefault_envs = V14_stable\n\n[env:V14_stable]\n'+base+'\n'
gift_base=base.replace('+<v14_stable/>','+<xmasgift/>')
gift_base=gift_base.replace('build_flags =\n','''build_unflags = -std=gnu++11
build_flags =
    -std=gnu++17
    -Isrc/xmasgift/virgin
    -DVIRGIN_EMBEDDED=1
    -DVIRGIN_EMBEDDED_HASH_KIB=16
    -DVIRGIN_EMBEDDED_MAX_PLY=20
''')
gift_ini='[platformio]\ndefault_envs = eChessGW_XMasGift\n\n[env:eChessGW_XMasGift]\n'+gift_base+'\n'
for folder,sources,ini in ((stable,freeze,stable_ini),(release,gift,gift_ini)):
    with zipfile.ZipFile(folder/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(sources.rglob('*')):
            if p.is_file():z.write(p,'src/'+sources.name+'/'+p.relative_to(sources).as_posix())
        z.writestr('platformio.ini',ini)
        z.write(repo/'LICENSE','LICENSE')
        z.write(folder/'README.md','README.md')
        if sources==gift:
            for n in ('test/xmasgift_test.cpp','tools/test_xmasgift.cmd','tools/test_xmasgift_device.py'):
                z.write(root/n,n)
    with zipfile.ZipFile(folder/'source.zip') as z:assert z.testzip() is None
    (folder/'SHA256.json').write_text(json.dumps({p.name:sha(p) for p in sorted(folder.iterdir()) if p.is_file()},indent=2)+'\n')
for folder in (freeze,gift):shutil.copytree(folder,repo/'src'/folder.name)
shutil.copy2(root/'platformio.ini',repo/'platformio.ini')
for n in ('test/xmasgift_test.cpp','tools/test_xmasgift.cmd','tools/test_xmasgift_device.py',
          'tools/prepare_xmasgift.py','tools/package_xmasgift_20261119.py','tools/update_xmasgift_virgin087.py',
          'docs/virgin_weihnachtsgimmick_20261119.md','docs/xmasgift_release_20261119.md'):
    shutil.copy2(root/n,repo/n)
# Exact current binary becomes the normal saved stable installer.
for n in ('bootloader.bin','partitions.bin','boot_app0.bin'):shutil.copy2(stable/n,docs/n)
shutil.copy2(stable/'firmware.bin',docs/'eChessGw-firmware.bin')
m=json.loads((stable/'manifest.json').read_text())
for part in m['builds'][0]['parts']:
    if part['offset']==65536:part['path']='eChessGw-firmware.bin'
(docs/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
page=docs/'index.html';s=page.read_text(encoding='utf-8')
s=re.sub(r'<h2>Release[^<]*V13 Move</h2>','<h2>Stable release &mdash; V14_stable</h2>',s,count=1)
s=s.replace('Chessnut Move and physical engine duels. ESP32-C3 SuperMini. Includes castling, en-passant and promotion-handling updates.',
    'Saved current SuperMini gateway, including virtual-board promotion negotiation. The optional Christmas version is available separately below.')
s=s.replace('releases/v13_move/README.md','releases/V14_stable/README.md',1).replace('V13 Move release notes and test status','V14_stable notes and test status',1)
s=s.replace('releases/v13_move/SHA256.json','releases/V14_stable/SHA256.json',1)
s=s.replace('https://github.com/parlue/eChessGw/tree/v13_move/src/v13_move','https://github.com/parlue/eChessGw/tree/main/src/v14_stable',1)
anchor='  <details class="installer" id="virtual-promotion-beta">'
assert s.count(anchor)==1
s=s.replace(anchor,'''  <details class="installer" id="xmasgift">
    <summary>eChessGW_XMasGift &mdash; Virgin 0.8.7 Adaptive (beta)</summary>
    <p>Play directly against Virgin on your board, without a cable computer or app.
      Complete board connection and piece learning first; F4 starts Virgin directly.
      Then use the starting position plus an extra queen on <strong>F4</strong>:
      white queen = you play White; black queen = you play Black.</p>
    <p>The centre flashes twice, then the corners twice. Remove the extra queen.
      Returning to the starting position later ends the game; use F4 again to restart.
      The one-second evaluation display always shows your advantage or disadvantage.</p>
    <p>Includes an embedded opening book and the improved 0.8.7 adaptive selection.
      Game over: White wins = E4/D5, Black wins = D4/E5, draw = all four centre squares.
      Result LEDs stay on until the full starting position is restored.</p>
    <p>Complete Certabo/Spectrum learning first. The previous engine was tested with
      Chessnut and virtual Certabo; 0.8.7 board gameplay still needs confirmation.</p>
    <esp-web-install-button manifest="beta/eChessGW_XMasGift/manifest.json"></esp-web-install-button>
    <p><a href="beta/eChessGW_XMasGift/README.md">Instructions and test status</a> &middot;
      <a href="beta/eChessGW_XMasGift/SHA256.json">Checksums</a></p>
  </details>

  <details class="installer" id="v13-fallback">
    <summary>Previous release: V13 Move</summary>
    <esp-web-install-button manifest="releases/v13_move/manifest.json"></esp-web-install-button>
  </details>

'''+anchor)
page.write_text(s,encoding='utf-8',newline='\n')
readme=repo/'README.md';s=readme.read_text(encoding='utf-8')
s=re.sub(r'\*\*Current release:.*?\n','''**Current saved stable release: V14_stable - 19 November 2026.** [Install](https://parlue.github.io/eChessGw/) · [Notes](docs/releases/V14_stable/README.md).

**eChessGW_XMasGift:** separate test version with built-in Virgin Adaptive, F4 queen gestures and a one-second human-perspective evaluation display. [Christmas installer](https://parlue.github.io/eChessGw/#xmasgift) · [Instructions and test limits](docs/beta/eChessGW_XMasGift/README.md). V13 Move and V12 remain selectable fallbacks.
''',s,count=1)
readme.write_text(s,encoding='utf-8',newline='\n')
(docs/'README.md').write_text('''# eChessGw Web Flasher

The normal installer is the saved **V14_stable** SuperMini firmware.
The separate **eChessGW_XMasGift** installer includes Virgin Adaptive.
V13 Move, V12 and previous betas remain available with unchanged bytes.

- V14_stable package: releases/V14_stable/.
- Christmas package: beta/eChessGW_XMasGift/.
- Certabo: starting position plus extra queens d3/d6; Spectrum: e3/e6.
- After learning and removing both queens, use an extra queen on F4 to start
  Virgin: white queen = human White, black queen = human Black.
- Read each package's instructions and hardware-test status before installing.

GitHub Pages: main branch, /docs. https://parlue.github.io/eChessGw/
''',encoding='utf-8',newline='\n')
assert before=={str(p):sha(p) for p in protected},'An old package changed'
print('Packaged V14_stable:',sha(stable/'firmware.bin'))
print('Packaged eChessGW_XMasGift:',sha(release/'firmware.bin'))
print('Protected existing package files:',len(protected))

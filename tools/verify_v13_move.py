from pathlib import Path
import json,hashlib,configparser,re
r=Path(__file__).resolve().parents[1]
release=r/'docs/releases/v13_move'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
checks=json.loads((release/'SHA256.json').read_text())
for name,digest in checks.items():assert sha(release/name)==digest,name
freeze=json.loads((release/'FREEZE.json').read_text())
assert sha(release/'eChessGw-firmware.bin')==freeze['released_firmware_sha256']
assert sha(r/'docs/eChessGw-firmware.bin')==freeze['released_firmware_sha256']
for row in freeze['files']:
 assert sha(r/row['frozen'])==row['frozen_sha256'],row['frozen']
 assert sha(r/row['active'])==row['public_active_sha256'],row['active']
frozen=(r/'src/v13_move').resolve()
for p in frozen.rglob('*'):
 if p.suffix not in ('.cpp','.h','.inc'):continue
 for name in re.findall(r'^\s*#\s*include\s*["<]([^">]+)[">]',p.read_text(encoding='utf-8'),re.M):
  local=(p.parent/name).resolve()
  if local.is_file():assert local.is_relative_to(frozen),(p,name)
  else:assert not (r/'include'/name).is_file(),(p,name)
for base in [r/'docs',release]:
 manifest=json.loads((base/'manifest.json').read_text())
 assert manifest['version']=='v13_move'
 for build in manifest['builds']:
  assert build['chipFamily']=='ESP32-C3'
  end=0
  for part in sorted(build['parts'],key=lambda p:p['offset']):
   p=base/part['path'];assert p.is_file()
   assert part['offset']>=end
   end=part['offset']+p.stat().st_size
   assert end<=4*1024*1024
cfg=configparser.ConfigParser(interpolation=None);cfg.read(r/'platformio.ini')
env=cfg['env:esp32-c3-superminiV13_Move']
assert 'extends' not in env and 'v13_move/' in env['build_src_filter']
assert '@ 7.0.1' in env['platform'] and '@ 2.5.1' in env['lib_deps']
assert '-<v13_move/>' in cfg['env:esp32-c3-supermini']['build_src_filter']
html=(r/'docs/index.html').read_text(encoding='utf-8')
assert 'manifest="manifest.json"' in html and 'releases/v12/manifest.json' in html
assert 'chessnut-move-beta' not in html
print('V13 Move verified:',len(checks),'assets,',len(freeze['files']),'source provenance rows; closed includes, pinned build and web manifests OK.')

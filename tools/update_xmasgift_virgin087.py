"""Import the local 0.8.7 engine, retaining the reviewed C3 allocation profile."""
from pathlib import Path
import hashlib, json, shutil

root = Path(__file__).resolve().parents[1]
upstream = Path(r'C:\Users\DirkSommerfeld\Documents\Projects\virgin\src')
target = root/'src/xmasgift/virgin'
backup = root/'backups/xmasgift-before-virgin087-20261120'
assert '"0.8.7"' in (upstream/'version.h').read_text()
assert not backup.exists(), 'One-shot import; backup already exists'
shutil.copytree(root/'src/xmasgift', backup/'source')
shutil.copy2(root/'test/xmasgift_test.cpp', backup/'xmasgift_test.cpp')
shutil.copy2(root/'.pio/build/eChessGW_XMasGift/firmware.bin', backup/'firmware.bin')
old_search = (target/'search.cpp').read_text()
names = [p.name for p in target.iterdir() if p.suffix in ('.h','.cpp') and p.name not in ('tt.h','tt.cpp')]
names += ['opening_book.h','opening_book_data.h','time_policy.h','version.h']
hashes = {}
for name in names:
    source = upstream/name
    shutil.copy2(source, target/name)
    hashes[name] = hashlib.sha256(source.read_bytes()).hexdigest()

s = (target/'search.cpp').read_text()
begin = s.index('// Zobrist keys of the current line')
end = s.index('\nvoid updatePv(', begin)
session = old_search[old_search.index('// These reductions'):old_search.index('\nvoid updatePv(')]
session = session.replace('    int16_t continuation[2048]{};',
    '    int16_t captureHistory[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB][PIECE_TYPE_NB]{};\n    int16_t continuation[2048]{};')
session += '\n#define g_captureHistory (sessionMemory->captureHistory)\n'
s = s[:begin] + session + s[end:]
s = s.replace('#include <vector>', '#include <vector>\n#include <new>')
def replace_once(before, after):
    global s
    assert s.count(before)==1, before
    s = s.replace(before,after)
replace_once('if (previous\n            && g_counterMove[pos.sideToMove()][from_sq(previous)][to_sq(previous)] == m)',
    'const auto& hint = g_counterMove[pos.sideToMove()][(previous ^ (previous >> 8)) & 255];\n        if (previous && hint.previous == previous && hint.reply == m)')
replace_once('g_counterMove[us][from_sq(previous)][to_sq(previous)] = best;',
    'g_counterMove[us][(previous ^ (previous >> 8)) & 255] = {previous, best};')
replace_once('Move quiets[256];', 'auto& quiets = list.moves; // Reuse already searched prefix; saves 512 bytes per ply.')
s += '\n' + old_search[old_search.index('// Called by the single cer4all search task;'):]
(target/'search.cpp').write_text(s)
with (target/'search.h').open('a') as out:
    out.write('\nbool allocateSessionSearch();\nvoid releaseSessionSearch();\n')
(target/'UPSTREAM_0.8.7.json').write_text(json.dumps({
    'version':'0.8.7', 'source':str(upstream), 'upstream_sha256':hashes,
    'local_adaptations':['search.cpp: per-search allocation, bounded countermoves, byte LMR, reused quiet list',
        'search.h: session allocation API', 'tt.cpp/tt.h: retained 8 KiB nonthrowing C3 table']
},indent=2)+'\n')
print('Imported Virgin 0.8.7; previous complete XmasGift source and firmware saved:',backup)

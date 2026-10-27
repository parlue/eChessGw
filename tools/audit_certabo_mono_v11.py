"""Compile the actual V11/current LED send functions against a recording writer.

Only host-side adapters are mocked. Production source/frozen sources are not edited.
"""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
out=root/'output/diagnostics/certabo-mono-v11-20261027'
out.mkdir(parents=True,exist_ok=True)
def send_function(path,name):
    s=path.read_text(encoding='utf-8-sig')
    a=s.index('void sendLeds() {');i=s.index('{',a);depth=1;j=i+1
    while depth:
        if s[j]=='{':depth+=1
        if s[j]=='}':depth-=1
        j+=1
    return s[a:j].replace('void sendLeds()',f'void {name}()',1)
(out/'actual_send_functions.h').write_text(
    send_function(root/'src/certabo_board_v11.cpp','sendV11')+'\n'+
    send_function(root/'src/certabo_board.cpp','sendCurrent')+'\n',encoding='utf-8')
(out/'frozen_protocol.cpp').write_text('#define certabo v11_certabo\n#include "'+(root/'src/certabo_protocol_v11.cpp').as_posix()+'"\n',encoding='utf-8')
manifest={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in (
    'src/certabo_board_v11.cpp','src/certabo_board.cpp','include/certabo_startup_v11.h',
    'include/certabo_startup.h','src/certabo_protocol_v11.cpp','src/certabo_protocol.cpp')}
(out/'source-hashes.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print('Prepared exact send-function comparison: '+str(out))

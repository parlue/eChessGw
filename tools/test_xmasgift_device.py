"""Exercise C3 engine/book moves using synthetic sensor diagnostics, no reset."""
from pathlib import Path
import re,time,serial
from serial.tools import list_ports

root=Path(__file__).resolve().parents[1]
out=root/'output/xmasgift-20261119'
ports=[p for p in list_ports.comports() if (p.serial_number or '').upper()=='70:AF:09:0D:A8:24']
assert len(ports)==1,'Expected the guarded C3 gateway USB device'
port=serial.Serial(port=None,baudrate=115200,timeout=.1,rtscts=False,dsrdtr=False)
port.dtr=False;port.rts=False;port.port=ports[0].device
transcript=[]
pending=''
def until(marker,seconds=40):
    global pending
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        pending+=port.read(max(1,min(8192,port.in_waiting))).decode(errors='replace')
        while '\n' in pending:
            line,pending=pending.split('\n',1);line=line.replace('\r','')
            transcript.append(line)
            if '[XMAS' in line: print(line,flush=True)
            if '[XMAS TEST] FAIL' in line or '[XMAS TEST] command rejected' in line or '[XMAS TEST] interrupted' in line:
                raise RuntimeError(line)
            if marker in line:return line
    raise RuntimeError('Timed out waiting for '+marker)
try:
    port.open()
    port.write(b'?xmas status\n');port.flush()
    line=until('[XMAS STATUS]',15)
    assert 'phase=0 own=0 job=0' in line,line
    baseline=int(re.search(r'heap=(\d+)',line)[1])
    for mode,number in [('white',1),('black',2),('cancel',3)]:
        start=len(transcript)
        port.write(('?xmas test '+mode+'\n').encode());port.flush()
        end=until('[XMAS TEST] PASS mode='+str(number),45)
        if mode!='cancel':
            assert int(re.search(r'plies=(\d+)',end)[1])>=4
            searches=[s for s in transcript[start:] if '[XMAS SEARCH]' in s]
            books=[s for s in transcript[start:] if '[XMAS BOOK]' in s]
            assert len(searches)+len(books)>=2,(searches,books)
            for s in searches:
                assert int(re.search(r'depth=(\d+)',s)[1])>=1,s
                assert int(re.search(r'stackFree=(\d+)',s)[1])>=2048,s
        time.sleep(.3)
        port.write(b'?xmas status\n');port.flush();status=until('[XMAS STATUS]',5)
        assert 'phase=0 own=0 job=0' in status,status
        heap=int(re.search(r'heap=(\d+)',status)[1])
        assert heap>=baseline-12288,(baseline,heap)
    print('PASS: C3 engine/book moves both colors, synthetic four-ply games, stopped games and released memory',flush=True)
finally:
    if port.is_open:port.close()
    (out/'device-search-test.txt').write_text('\n'.join(transcript)+'\n',encoding='utf-8')

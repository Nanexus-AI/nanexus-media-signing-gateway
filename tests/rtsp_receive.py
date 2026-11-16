# SPDX-License-Identifier: Apache-2.0
"""Independent bounded RTSP/TCP receiver; no media library or signing dependencies."""
import socket,time,struct,json,sys,argparse,signal
from pathlib import Path
from urllib.parse import urlsplit
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('url'); ap.add_argument('output',type=Path)
ap.add_argument('--seconds',type=int,default=0,help='stop at a complete AU after this interval; zero waits for EOF')
ap.add_argument('--wall',type=int,default=60)
ap.add_argument('--max-bytes',type=int,default=256*1024*1024)
a=ap.parse_args()
if not 1<=a.wall<=650 or not 0<=a.seconds<=600 or not 1<=a.max_bytes<=512*1024*1024: ap.error('bounds exceeded')
url=a.url; root=a.output; root.mkdir(parents=True,exist_ok=True); u=urlsplit(url)
if any((root/name).exists() for name in ['received.h264','receiver-rtp.bin','receiver.json']): ap.error('refuse existing evidence')
stop=False
def request_stop(*_):
 global stop
 stop=True
signal.signal(signal.SIGTERM,request_stop)
signal.signal(signal.SIGINT,request_stop)
begin=time.monotonic(); deadline=begin+a.wall; buffer=b''; statuses=[]; packets=0; bytecount=0
raw=(root/'receiver-rtp.bin').open('xb'); media=(root/'received.h264').open('xb')
seq=None; ssrc=None; stamp=None; fu=None; group=[]; au_rows=[]; nal_count=0; error=None; eof=False

def fill(n):
 global buffer,eof
 while len(buffer)<n:
  if time.monotonic()>deadline: raise TimeoutError('receiver wall ceiling')
  data=s.recv(65536)
  if not data: eof=True; raise EOFError()
  buffer+=data

def response():
 global buffer
 while b'\r\n\r\n' not in buffer: fill(len(buffer)+1)
 head,rest=buffer.split(b'\r\n\r\n',1); rows=head.decode().split('\r\n'); status=int(rows[0].split()[1]); statuses.append(status)
 headers=dict((k.lower(),v.strip()) for k,v in (line.split(':',1) for line in rows[1:] if ':' in line))
 buffer=rest; size=int(headers.get('content-length','0')); fill(size); body=buffer[:size]; buffer=buffer[size:]
 if status!=200: raise RuntimeError('RTSP status '+str(status))
 return headers,body

def request(method,target,headers=''):
 global cseq
 s.sendall(f'{method} {target} RTSP/1.0\r\nCSeq: {cseq}\r\n{headers}\r\n'.encode()); cseq+=1
 return response()

def rtp(p):
 global seq,ssrc,stamp,fu,group,packets,bytecount,nal_count
 assert len(p)>=12 and p[0]>>6==2
 sn,ts,ss=struct.unpack('!HII',p[2:12]); marker=bool(p[1]&128); pt=p[1]&127
 assert pt==96
 if seq is not None: assert sn==(seq+1)&65535 and ss==ssrc,'RTP gap/identity'
 seq=sn; ssrc=ss
 offset=12+4*(p[0]&15); assert offset<=len(p)
 if p[0]&16:
  assert offset+4<=len(p); offset+=4+4*struct.unpack('!H',p[offset+2:offset+4])[0]
 end=len(p)
 if p[0]&32: assert p[-1] and p[-1]<=end-offset; end-=p[-1]
 assert offset<end
 d=p[offset:end]; typ=d[0]&31
 if stamp is None: stamp=ts
 assert ts==stamp,'timestamp changed without marker'
 if typ==28:
  assert len(d)>=3 and not d[1]&32
  identity=(d[0]&0xe0)|(d[1]&31)
  if d[1]&128: assert fu is None; fu=bytes([identity])
  else: assert fu is not None and fu[0]==identity
  fu+=d[2:]; assert len(fu)<=8*1024*1024
  if d[1]&64: group.append(fu); fu=None
 elif typ==24:
  assert fu is None
  pos=1
  while pos<len(d):
   assert pos+2<=len(d); size=struct.unpack('!H',d[pos:pos+2])[0]; pos+=2
   assert size and pos+size<=len(d); group.append(d[pos:pos+size]); pos+=size
 else: assert 1<=typ<=23 and fu is None; group.append(d)
 packets+=1; bytecount+=len(p); assert packets<=2000000 and bytecount<=a.max_bytes
 assert sum(map(len,group))<=8*1024*1024 and len(group)<=4096
 if marker:
  assert fu is None
  au_rows.append({'timestamp':ts,'nal_start':nal_count,'nals':len(group),'types':[x[0]&31 for x in group]})
  for n in group: media.write(b'\0\0\0\1'+n)
  nal_count+=len(group); group=[]; stamp=None

try:
 while True:
  try: s=socket.create_connection((u.hostname,u.port),timeout=1); break
  except OSError:
   if time.monotonic()-begin>12: raise
   time.sleep(.05)
 s.settimeout(5); cseq=1
 headers,sdp=request('DESCRIBE',url,'Accept: application/sdp\r\n')
 controls=[x.split(':',1)[1] for x in sdp.decode().splitlines() if x.startswith('a=control:') and x!='a=control:*']
 control=controls[-1]; target=control if control.startswith('rtsp://') else url.rstrip('/')+'/'+control
 headers,_=request('SETUP',target,'Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n')
 session=headers['session'].split(';')[0]
 request('PLAY',url,f'Session: {session}\r\n')
 while True:
  fill(4); assert buffer[0]==36,'unexpected RTSP framing'; channel=buffer[1]; n=struct.unpack('!H',buffer[2:4])[0]; fill(n+4)
  payload=buffer[4:n+4]; buffer=buffer[n+4:]
  if not channel&1:
   raw.write(struct.pack('<I',n)+payload); rtp(payload)
   if not group and fu is None and (stop or (a.seconds and time.monotonic()-begin>=a.seconds)): break
except EOFError: pass
except Exception as e: error=type(e).__name__+': '+str(e)
finally:
 raw.close(); media.close()
 try: s.close()
 except NameError: pass
 result={'elapsed_seconds':time.monotonic()-begin,'RTSP_statuses':statuses,'packets':packets,'RTP_bytes':bytecount,'aus':len(au_rows),'nals':nal_count,'eof':eof,'incomplete_tail_nals':len(group),'incomplete_fu':fu is not None,'error':error}
 (root/'receiver.json').write_text(json.dumps(result,indent=2)+'\n'); (root/'receiver-aus.json').write_text(json.dumps(au_rows,indent=2)+'\n')
 print(json.dumps(result)); sys.exit(1 if error else 0)

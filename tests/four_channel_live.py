# SPDX-License-Identifier: Apache-2.0
"""Bounded native four-channel lab: disposable sources/identity and independent checks.

Requires local ffmpeg/ffprobe/OpenSSL, Docker with cached MediaMTX 1.15.3,
a native Gateway build and an independently built video-trust verifier.
Nothing is downloaded. Never point output at existing evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
UUID = bytes.fromhex('005bc93f2d715e95ada4796f90877a6f')
SOURCE_IMAGE = 'bluenviron/mediamtx@sha256:32ecec42d5c716f0b85809b11b6c32f54126546ba89fc85ac57dbcf74738bb4e'
SOURCE_NAME = 'nanexus-four-channel-source'


def run(args, **kw):
    return subprocess.run([str(x) for x in args], check=True, timeout=kw.pop('timeout', 45), **kw)


def write_json(path, obj):
    Path(path).write_text(json.dumps(obj, indent=2) + '\n')


def split_nals(data):
    marks = list(re.finditer(b'\x00\x00(?:\x00)?\x01', data))
    if not marks or marks[0].start() != 0:
        raise ValueError('not Annex-B')
    return [data[m.end():marks[i+1].start() if i+1<len(marks) else len(data)]
            for i, m in enumerate(marks)]


def prepare(root):
    root.mkdir(parents=True, mode=0o700)
    pki = root / 'pki'; pki.mkdir(mode=0o700)
    opts = dict(stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-sha256','-days','2',
         '-subj','/CN=Disposable Four Channel Test CA','-addext','basicConstraints=critical,CA:TRUE',
         '-addext','keyUsage=critical,keyCertSign,cRLSign','-keyout',pki/'ca.key.pem','-out',pki/'ca.pem'],**opts)
    run(['openssl','req','-newkey','rsa:2048','-nodes','-sha256','-subj','/CN=Four Channel Gateway Test',
         '-keyout',pki/'signer.key.pem','-out',pki/'signer.csr.pem'],**opts)
    (pki/'signer.ext').write_text('basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n')
    run(['openssl','x509','-req','-sha256','-days','2','-in',pki/'signer.csr.pem',
         '-CA',pki/'ca.pem','-CAkey',pki/'ca.key.pem','-CAcreateserial',
         '-extfile',pki/'signer.ext','-out',pki/'signer.cert.pem'],**opts)
    (pki/'signer-chain.pem').write_bytes((pki/'signer.cert.pem').read_bytes()+(pki/'ca.pem').read_bytes())
    for path in pki.iterdir(): path.chmod(0o600)
    manifest = {}
    for i in range(1,5):
        raw = root/f'cam{i}.h264'
        filters = f'testsrc2=size=1920x1080:rate=15,drawbox=x={i*200}:y=100:w=140:h=140:color=red:t=fill'
        run(['ffmpeg','-v','error','-nostdin','-f','lavfi','-i',filters,'-frames:v','60',
             '-c:v','libx264','-threads','1','-preset','ultrafast','-tune','zerolatency','-pix_fmt','yuv420p',
             '-b:v','2M','-minrate','2M','-maxrate','2M','-bufsize','2M',
             '-x264-params','nal-hrd=cbr:filler=1:keyint=30:min-keyint=30:scenecut=0:repeat-headers=1',
             '-bf','0','-an','-f','h264',raw],timeout=60)
        probe = json.loads(run(['ffprobe','-v','error','-show_packets','-show_entries','packet=pos,size',
                              '-of','json',raw],capture_output=True,text=True).stdout)
        offset=0; rows=[]
        for n,p in enumerate(probe['packets']):
            assert int(p['pos'])==offset
            size=int(p['size']); rows.append(f'{offset} {size} {1000+n*1000//15} {1000+n*1000//15} {1000//15}')
            offset+=size
        assert len(rows)==60 and offset==raw.stat().st_size
        (root/f'cam{i}.tsv').write_text('\n'.join(rows)+'\n')
        run(['ffmpeg','-v','error','-nostdin','-r','15','-i',raw,'-c:v','copy',root/f'cam{i}.mp4'])
        manifest[f'cam{i}']={'sha256':hashlib.sha256(raw.read_bytes()).hexdigest(),'frames':60,
                             'bytes':offset,'average_bitrate':offset*8/4}
    write_json(root/'fixtures.json',manifest)
    return manifest


def config_text(fixtures, host='127.0.0.1', bind='127.0.0.1', base=18554):
    text = f'[gateway]\nsigning.key_file={fixtures}/pki/signer.key.pem\nsigning.certificate_file={fixtures}/pki/signer-chain.pem\n'
    for i in range(1,5):
        text += f'\n[channel.cam{i}]\ncamera.url=rtsp://{host}:18654/source/cam{i}\noutput.url=rtsp://{bind}:{base+i-1}/signed/camera{i}\n'
    return text


def terminate(child):
    if child and child.poll() is None:
        child.terminate()
        try: child.wait(timeout=12)
        except subprocess.TimeoutExpired:
            child.kill(); child.wait(timeout=3)


class Sources:
    def __init__(self, fixtures, out):
        self.fixtures=fixtures; self.out=out; self.children={}; self.logs=[]; self.owned=False
    def __enter__(self):
        self.out.mkdir(parents=True)
        conf=self.out/'mediamtx.yml'
        conf.write_text('logLevel: warn\nrtspAddress: :18654\nrtspTransports: [tcp]\nrtmp: no\nhls: no\nwebrtc: no\nsrt: no\npaths:\n  all_others: {}\n')
        try:
            run(['docker','run','--pull=never','-d','--name',SOURCE_NAME,'--network','host','--read-only',
                 '--cap-drop','ALL','--security-opt','no-new-privileges','--memory','128m','--cpus','0.5',
                 '-v',f'{conf.resolve()}:/mediamtx.yml:ro',SOURCE_IMAGE],capture_output=True)
            self.owned=True
            deadline=time.monotonic()+8
            while True:
                try:
                    with socket.create_connection(('127.0.0.1',18654),timeout=.3): break
                except OSError:
                    if time.monotonic()>deadline: raise
                    time.sleep(.1)
            for i in range(1,5): self.start(i)
            time.sleep(2)
            assert all(p.poll() is None for p in self.children.values()),'source producer failed'
            return self
        except BaseException:
            self.__exit__(None,None,None); raise
    def start(self,i):
        log=(self.out/f'producer-{i}-{time.time_ns()}.log').open('wb'); self.logs.append(log)
        p=subprocess.Popen(['ffmpeg','-v','warning','-nostdin','-re','-stream_loop','-1','-i',str(self.fixtures/f'cam{i}.mp4'),
                            '-map','0:v:0','-c:v','copy','-bsf:v','h264_mp4toannexb','-an','-f','rtsp','-rtsp_transport','tcp',
                            f'rtsp://127.0.0.1:18654/source/cam{i}'],stdout=log,stderr=subprocess.STDOUT)
        self.children[i]=p; return p.pid
    def stop(self,i): terminate(self.children.pop(i,None))
    def __exit__(self,*_):
        for child in self.children.values(): terminate(child)
        if self.owned:
            r=subprocess.run(['docker','logs',SOURCE_NAME],capture_output=True,timeout=10)
            (self.out/'mediamtx.log').write_bytes(r.stdout+r.stderr)
            run(['docker','stop','-t','5',SOURCE_NAME],capture_output=True,timeout=15)
            run(['docker','rm',SOURCE_NAME],capture_output=True)
            self.owned=False
        for log in self.logs: log.close()


def receive(url, out, seconds=0, wall=60):
    out.mkdir(parents=True)
    log=(out/'receiver.log').open('wb')
    p=subprocess.Popen([sys.executable,str(ROOT/'tests/rtsp_receive.py'),url,str(out),
                        '--seconds',str(seconds),'--wall',str(wall)],stdout=log,stderr=subprocess.STDOUT)
    log.close(); return p


def verify(verifier, ca, path, out):
    result={}
    for verb in ['verify','inspect']:
        p=subprocess.run([str(verifier),verb,'--codec','h264','--ca',str(ca),'--json',str(path)],capture_output=True,timeout=30)
        (out/f'{verb}.json').write_bytes(p.stdout); (out/f'{verb}.stderr').write_bytes(p.stderr)
        result[verb]={'exit':p.returncode,'report':json.loads(p.stdout)}
    return result


def preservation(fixtures, i, received):
    known=[]
    for j in range(1,5):
        data=(fixtures/f'cam{j}.h264').read_bytes()
        rows=[list(map(int,row.split())) for row in (fixtures/f'cam{j}.tsv').read_text().splitlines()]
        known.append([tuple(split_nals(data[o:o+n])) for o,n,*_ in rows])
    nals=split_nals((received/'received.h264').read_bytes())
    rows=json.loads((received/'receiver-aus.json').read_text())
    matched=[]; seis=0
    for row in rows:
        au=nals[row['nal_start']:row['nal_start']+row['nals']]
        original=tuple(n for n in au if not ((n[0]&31)==6 and UUID in n))
        seis+=len(au)-len(original)
        # Require exact admitted source bytes, including parameter sets and
        # original encoder SEIs; remove only the newly generated ONVIF SEIs.
        candidates=[k for k,ref in enumerate(known[i-1]) if ref==original]
        assert candidates, f'cam{i}: output original AU differs from its source fixture'
        assert not any(original in other for j,other in enumerate(known) if j!=i-1),'cross-channel payload'
        if matched: assert (matched[-1]+1)%60 in candidates,'original AU order/gap'
        matched.append((matched[-1]+1)%60 if matched else candidates[0])
    assert matched and seis
    return {'original_aus':len(matched),'signing_seis':seis,'original_nal_bytes_and_order':'exact','other_channel_match':False}


def host_test(binary, fixtures, verifier, out, seconds):
    out.mkdir(parents=True)
    conf=out/'gateway.conf'; conf.write_text(config_text(fixtures))
    workers=[]; readers=[]; logs=[]; samples=[]
    with Sources(fixtures,out/'sources'):
        try:
            for i in range(1,5):
                log=(out/f'gateway-cam{i}.log').open('wb'); logs.append(log)
                workers.append(subprocess.Popen([str(binary),'--config',str(conf),'--channel',f'cam{i}',
                                                 '--duration',str(seconds)],stdout=log,stderr=subprocess.STDOUT))
                readers.append(receive(f'rtsp://127.0.0.1:{18553+i}/signed/camera{i}',out/f'cam{i}',wall=seconds+25))
            begin=time.monotonic()
            while any(p.poll() is None for p in workers+readers):
                assert time.monotonic()-begin<seconds+30,'host wall bound'
                rss=[]
                for p in workers:
                    try: rss.append(int(Path(f'/proc/{p.pid}/statm').read_text().split()[1])*os.sysconf('SC_PAGE_SIZE'))
                    except FileNotFoundError: rss.append(0)
                assert all(n<128*1024*1024 for n in rss),'worker RSS limit'
                samples.append([time.monotonic()-begin,rss]); time.sleep(.2)
            assert all(p.returncode==0 for p in workers+readers),'worker/receiver failure; preserve logs'
        finally:
            for p in workers+readers: terminate(p)
            for log in logs: log.close()
    result={}
    for i in range(1,5):
        rx=out/f'cam{i}'; check=json.loads((rx/'receiver.json').read_text())
        assert check['error'] is None and not check['incomplete_fu'] and not check['incomplete_tail_nals']
        checks=verify(verifier,fixtures/'pki/ca.pem',rx/'received.h264',rx)
        assert checks['verify']['exit']==0 and checks['inspect']['exit']==0,'independent complete verification failed'
        result[f'cam{i}']={'receiver':check,'verification':checks,'preservation':preservation(fixtures,i,rx)}
    result['resources']={'samples':samples,'peak_worker_RSS_bytes':[max(s[1][i] for s in samples) for i in range(4)]}
    write_json(out/'results.json',result)
    return {k:v['preservation'] for k,v in result.items() if k.startswith('cam')}


def main():
    def interrupted(signum, _frame):
        raise SystemExit(128+signum)
    signal.signal(signal.SIGTERM, interrupted)
    ap=argparse.ArgumentParser(description=__doc__)
    sub=ap.add_subparsers(dest='action',required=True)
    p=sub.add_parser('prepare'); p.add_argument('output',type=Path)
    p=sub.add_parser('host'); p.add_argument('binary',type=Path); p.add_argument('fixtures',type=Path)
    p.add_argument('verifier',type=Path); p.add_argument('output',type=Path); p.add_argument('--seconds',type=int,default=20)
    a=ap.parse_args()
    if a.output.exists(): ap.error('refuse existing output directory')
    if a.action=='prepare': result=prepare(a.output.resolve())
    else:
        if not 5<=a.seconds<=180: ap.error('seconds outside 5..180')
        result=host_test(a.binary.resolve(),a.fixtures.resolve(),a.verifier.resolve(),a.output.resolve(),a.seconds)
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()

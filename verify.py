"""Targeted real-PDF integration tests. Requires existing PyMuPDF and Pillow; installs nothing."""
from pathlib import Path
import ctypes, hashlib, json, os, shutil, subprocess, sys, time
import pymupdf
from PIL import Image

ROOT=Path(__file__).resolve().parent
WORK=ROOT/'work'/time.strftime('verify-%Y%m%d-%H%M%S')
WORK.mkdir(parents=True)
DEPLOY=WORK/'中文 日本語 portable'
shutil.copytree(ROOT/'dist-test',DEPLOY)
EXE=DEPLOY/'PDF-to-JPG_QT.exe'
ENV=os.environ.copy()
for key in list(ENV):
    if key.startswith('QT_') or key in ('QML2_IMPORT_PATH','QML_IMPORT_PATH'):ENV.pop(key,None)
ENV['PATH']=str(Path(os.environ['SystemRoot'])/'System32')
checks=[]
class Memory(ctypes.Structure):
    _fields_=[('cb',ctypes.c_ulong),('faults',ctypes.c_ulong)]+[(n,ctypes.c_size_t) for n in ('peakWS','ws','peakPaged','paged','peakNonpaged','nonpaged','pagefile','peakPagefile')]
def run(name,files,mode='--batch',envs=None,race=None,**options):
    cfg=dict(files=[str(p) for p in files],output=str(WORK/name),report=str(WORK/(name+'.json')),screenshot=str(WORK/(name+'.png')))
    cfg.update(options);config=WORK/(name+'-config.json');config.write_text(json.dumps(cfg,ensure_ascii=False),encoding='utf8')
    start=time.perf_counter();p=subprocess.Popen([str(EXE),mode,str(config)],cwd=WORK,env=dict(ENV,**(envs or {})),stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)
    peak=0; injected=False
    while p.poll() is None:
        if race and not injected:
            target=WORK/name/'cancel-100pages'/'cancel-100pages_p0001.jpg'
            if target.parent.exists():
                if race=='directory':target.mkdir()
                else:target.write_bytes(b'EXISTING CORRUPT JPG MUST SURVIVE')
                injected=True
        m=Memory();m.cb=ctypes.sizeof(m)
        if ctypes.windll.psapi.GetProcessMemoryInfo(ctypes.c_void_p(int(p._handle)),ctypes.byref(m),m.cb):peak=max(peak,m.peakWS)
        if time.perf_counter()-start>120:p.kill();raise RuntimeError(name+' timeout')
        time.sleep(.02)
    stdout,stderr=p.communicate();(WORK/(name+'-stderr.txt')).write_bytes(stderr)
    r=json.loads(Path(cfg['report']).read_text(encoding='utf8'));r['measuredPeakWorkingSetMiB']=round(peak/2**20,1);r['wallSeconds']=round(time.perf_counter()-start,3)
    checks.append(dict(name=name,exit=p.returncode,result=r));print(name,p.returncode,r.get('saved',0),r.get('elapsedMs',0),r['measuredPeakWorkingSetMiB'],flush=True)
    return r
def pdf(path,pages=2,width=288,height=216,encrypted=False,textured=False):
    doc=pymupdf.open()
    if textured:
        import random,io
        pixels=random.Random(42).randbytes(700*900*3);im=Image.frombytes('RGB',(700,900),pixels);b=io.BytesIO();im.save(b,format='JPEG',quality=90);img=b.getvalue()
    for i in range(pages):
        page=doc.new_page(width=width,height=height)
        if textured:page.insert_image(page.rect,stream=img)
        page.insert_text((20,30),f'Qt test page {i+1}',fontsize=16)
        page.draw_rect(pymupdf.Rect(20,50,130,100),fill=(.1,.4,.8))
    if encrypted:doc.save(str(path),encryption=pymupdf.PDF_ENCRYPT_AES_256,user_pw='test-pass',owner_pw='owner')
    else:doc.save(str(path))
    doc.close()
def states(r):return [f['status'] for f in r['files']]
normal=WORK/'中文 にほんご.pdf';pdf(normal)
locked=WORK/'加密.pdf';pdf(locked,1,encrypted=True)
broken=WORK/'broken.pdf';broken.write_bytes(b'%PDF-1.7\ninvalid')
missing=WORK/'missing.pdf'
huge=WORK/'huge.pdf';pdf(huge,1,width=14000,height=14000)
bench=WORK/'A4-texture-24pages.pdf';pdf(bench,24,width=595,height=842,textured=True)
for dpi in (150,200,300,600):
    r=run(f'dpi{dpi}',[normal],dpi=dpi)
    assert states(r)==['completed'] and r['saved']==2
    image=next(Path(r['files'][0]['folder']).glob('*.jpg'))
    with Image.open(image) as im:
        assert im.size==(dpi*4,dpi*3),(im.size,dpi)
        assert abs(im.info['dpi'][0]-dpi)<=1 and im.mode=='RGB'
        assert min(im.getpixel((im.width-1,im.height-1)))>=250
r=run('mixed',[normal,locked,broken,missing],password='test-pass')
assert states(r)==['completed','completed','failed','failed']
r=run('wrong-password',[locked,normal],password='bad');assert states(r)==['failed','completed']
r=run('skip-password',[locked,normal]);assert states(r)==['failed','completed']
r=run('oversize',[huge,normal]);assert states(r)==['failed','completed']
r=run('budget',[bench],dpi=600,memoryMiB=128);assert states(r)==['failed'] and r['peakImageReservationBytes']==0
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (WORK/'dpi200').rglob('*.jpg')}
r=run('repeat',[normal],output=str(WORK/'dpi200'));assert states(r)==['completed'] and '(2)' in r['files'][0]['folder']
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in before.items())
badOutput=WORK/'output-is-file';badOutput.write_text('unchanged')
r=run('output-error',[normal],output=str(badOutput));assert r['fatal'] and badOutput.read_text()=='unchanged'
longdir=WORK
while len(str(longdir))<290:longdir=longdir/('長路徑日本語'+str(len(str(longdir))))
longdir.mkdir(parents=True);longpdf=longdir/'測試.pdf';pdf(longpdf,1)
r=run('longpath',[longpdf],output=str(longdir/'輸出'));assert states(r)==['completed']
for count in (1,4):
    r=run('bench'+str(count),[bench],dpi=300,encoders=count)
    assert r['saved']==24 and r['peakImageReservationBytes']<=512*2**20
cancelPdf=WORK/'cancel-100pages.pdf';pdf(cancelPdf,100,width=595,height=842)
r=run('cancel',[cancelPdf,normal],dpi=600,cancelMs=750,encoders=4)
assert r['cancelRequested'] and states(r)[-1]=='not_started' and states(r)[0]=='cancelled'
assert 0<r['saved']<100 and len(list((WORK/'cancel').rglob('*.jpg')))==r['saved']
for dark in (False,True):
    r=run('gui-dark' if dark else 'gui-light',[normal],mode='--smoke',dark=dark)
    assert r['saved']==2 and (WORK/('gui-dark.png' if dark else 'gui-light.png')).exists()
r=run('gui-cancel',[cancelPdf,normal],mode='--smoke',dark=True,dpi=600,cancelMs=500)
assert r['cancelRequested'] and states(r)[0]=='cancelled' and r['guiHeartbeatTicks']>=5
r=run('gui-password',[locked,normal],mode='--smoke',password='test-pass');assert states(r)==['completed','completed']
r=run('gui-password-cancel',[locked,normal],mode='--smoke',waitPassword=True,cancelMs=400);assert r['cancelRequested'] and states(r)==['cancelled','not_started']
(WORK/'summary.json').write_text(json.dumps(dict(passed=True,checks=checks),ensure_ascii=False,indent=2),encoding='utf8')
print('PASS',WORK,flush=True)

# Fault injection: each process has a timeout, so any join/drain deadlock fails the run.
for index in (1,2,4):
    r=run('thread-fail'+str(index),[normal],encoders=4,envs={'PDF2JPG_FAIL_THREAD':str(index)})
    assert states(r)==['failed'] and r['saved']==0 and 'injected' in str(r['files'][0]['errors'])
for key in ('SUBMIT','WRITE'):
    r=run('fail-'+key,[normal],envs={'PDF2JPG_FAIL_'+key:'1'})
    assert states(r)==['failed'] and r['saved']==0 and r['files'][0]['pageErrors']==2
    assert not list((WORK/('fail-'+key)).rglob('*.tmp'))
for i in range(12):
    r=run('callback-fail'+str(i),[normal],encoders=4,envs={'PDF2JPG_FAIL_CALLBACK':'1'})
    assert r['cancelRequested'] and r['files'][0]['errors'],r
for kind in ('file','directory'):
    r=run('race-'+kind,[cancelPdf],dpi=600,cancelMs=900,race=kind)
    target=WORK/('race-'+kind)/'cancel-100pages'/'cancel-100pages_p0001.jpg'
    assert target.is_dir() if kind=='directory' else target.read_bytes()==b'EXISTING CORRUPT JPG MUST SURVIVE'
    assert r['files'][0]['pageErrors']>=1
    assert not list((WORK/('race-'+kind)).rglob('*.tmp'))
r=run('resume-rejected',[normal],resume=True);assert r['fatal'] and r['saved']==0
# A directory blocking the output root is a real filesystem failure, not injected.
block=WORK/'blocked-root';block.write_bytes(b'preserve')
r=run('blocked-child',[normal],output=str(block/'child'));assert r['fatal'] and block.read_bytes()==b'preserve'
# Two independent processes claiming the same stem must get separate folders.
shared=WORK/'concurrent';processes=[]
for i in range(2):
    cfg=dict(files=[str(normal)],output=str(shared),report=str(WORK/f'concurrent-{i}.json'))
    c=WORK/f'concurrent-{i}-config.json';c.write_text(json.dumps(cfg),encoding='utf8')
    processes.append(subprocess.Popen([str(EXE),'--batch',str(c)],env=ENV,creationflags=subprocess.CREATE_NO_WINDOW))
for proc in processes:assert proc.wait(timeout=20)==0
rs=[json.loads((WORK/f'concurrent-{i}.json').read_text(encoding='utf8')) for i in range(2)]
assert all(r['saved']==2 for r in rs) and rs[0]['files'][0]['folder']!=rs[1]['files'][0]['folder']
checks.append(dict(name='concurrent-processes',passed=True))
stress=WORK/'A4-texture-160pages.pdf';pdf(stress,160,width=595,height=842,textured=True)
r=run('stress160',[stress],dpi=300,encoders=4,memoryMiB=384)
assert r['saved']==160 and states(r)==['completed'] and r['peakImageReservationBytes']<=384*2**20
for image in (WORK/'stress160').rglob('*.jpg'):
    with Image.open(image) as im:im.load();assert im.size==(2480,3509)
r=run('final-failure',[normal],envs={'PDF2JPG_FAIL_FINAL':'1'});assert r['fatal'] and r['cancelRequested']
r=run('gui-close-password',[locked,normal],mode='--smoke',waitPassword=True,closeMs=400)
assert r['cancelRequested'] and states(r)==['cancelled','not_started']
r=run('gui-close-render',[cancelPdf,normal],mode='--smoke',dpi=600,closeMs=500)
assert r['cancelRequested'] and states(r)[-1]=='not_started'
(WORK/'summary.json').write_text(json.dumps(dict(passed=True,checks=checks),ensure_ascii=False,indent=2),encoding='utf8')
print('EXTENDED PASS',len(checks),WORK,flush=True)

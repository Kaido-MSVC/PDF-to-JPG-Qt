"""Windows v1.1 integration checks; uses installed PyMuPDF/Pillow, no installs."""
from pathlib import Path
import ctypes, hashlib, json, os, shutil, subprocess, time
import pymupdf
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT/'work'/time.strftime('resume-%Y%m%d-%H%M%S')
WORK.mkdir(parents=True)
DEPLOY = WORK/'中文 日本語 portable'
shutil.copytree(ROOT/'dist-test', DEPLOY)
EXE = DEPLOY/'PDF-to-JPG_QT.exe'
ENV = {k:v for k,v in os.environ.items() if not k.startswith(('QT_', 'QML'))}
ENV['PATH'] = str(Path(os.environ['SystemRoot'])/'System32')
checks=[]

def pdf(path, pages=3, text='resume', width=288, height=216):
    doc=pymupdf.open()
    for i in range(pages):
        page=doc.new_page(width=width,height=height)
        page.insert_text((20,30),f'{text} {i}')
    doc.save(path);doc.close()

def spawn(name, files, envs=None, mode='--batch', **options):
    cfg=dict(files=list(map(str,files)),output=str(WORK/'output'),report=str(WORK/(name+'.json')),
             screenshot=str(WORK/(name+'.png')))
    cfg.update(options)
    config=WORK/(name+'-config.json');config.write_text(json.dumps(cfg,ensure_ascii=False),encoding='utf8')
    err=(WORK/(name+'-stderr.txt')).open('wb')
    proc=subprocess.Popen([str(EXE),mode,str(config)],env=dict(ENV,**(envs or {})),cwd=WORK,
                          stdout=subprocess.DEVNULL,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
    err.close()
    return proc,cfg

def finish(name, proc, cfg):
    proc.wait(timeout=60)
    result=json.loads(Path(cfg['report']).read_text(encoding='utf8'))
    checks.append(dict(name=name,exit=proc.returncode,result=result))
    (WORK/'summary.json').write_text(json.dumps(dict(passed=False,checks=checks),ensure_ascii=False,indent=2),encoding='utf8')
    print(name,result.get('saved'),result.get('skipped'),[f['status'] for f in result.get('files',[])],flush=True)
    return result

def run(name,files=None,**kwargs):
    p,c=spawn(name,files or [SOURCE],**kwargs);return finish(name,p,c)

def ok(r):return not r.get('fatal') and all(f['status']=='completed' for f in r['files'])
def failed(r):return bool(r.get('fatal')) or any(f['status']=='failed' for f in r.get('files',[]))
def snap(folder):
    return {p.name:(hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else 'directory',p.stat().st_mtime_ns)
            for p in folder.iterdir() if p.name!='.pdf2jpg.lock'}
def clone(name):
    dest=WORK/name;shutil.copytree(BATCH,dest);return dest
def rejected(name,folder,files=None,**opts):
    before=snap(folder)
    r=run(name,files,resume=True,resumeFolder=str(folder),**opts)
    assert failed(r) and r.get('saved',0)==0 and r.get('skipped',0)==0,r
    assert snap(folder)==before,name+' mutated old files'
    return r

SOURCE=WORK/'測試 にほんご.pdf';pdf(SOURCE)
r=run('normal');assert ok(r) and r['saved']==3
BATCH=Path(r['files'][0]['folder'])
manifest = json.loads((BATCH/'.pdf2jpg-batch.json').read_text(encoding='utf8'))
assert manifest['schema'] == 2
assert manifest['qtPdfSha256'] == hashlib.sha256((DEPLOY/'Qt6Pdf.dll').read_bytes()).hexdigest()
assert manifest['outputContract'].startswith('qt-pdf-annotations/')
before=snap(BATCH)
r=run('resume-complete',resume=True,resumeFolder=str(BATCH))
assert ok(r) and r['saved']==0 and r['skipped']==3 and snap(BATCH)==before
r=run('default-repeat');assert ok(r) and r['saved']==3 and Path(r['files'][0]['folder'])!=BATCH
r=run('resource-settings',resume=True,resumeFolder=str(BATCH),encoders=4,memoryMiB=128)
assert ok(r) and r['skipped']==3
rejected('multiple-sources',clone('multiple-sources'),files=[SOURCE,SOURCE])
for key,value in [('dpi',300),('quality',91)]:rejected('changed-'+key,clone('batch-'+key),**{key:value})
folder=clone('source-change')
stat=SOURCE.stat();original=SOURCE.read_bytes()
SOURCE.write_bytes(original.replace(b'resume',b'change'));os.utime(SOURCE,ns=(stat.st_atime_ns,stat.st_mtime_ns))
assert SOURCE.stat().st_size==len(original)
# PDFs commonly compress their streams: replace one metadata byte instead if text is compressed.
if SOURCE.read_bytes()==original:
    data=bytearray(original);pos=data.index(b'%PDF-1.7')+7;data[pos]=ord('6');SOURCE.write_bytes(data)
    os.utime(SOURCE,ns=(stat.st_atime_ns,stat.st_mtime_ns))
rejected('changed-content-same-size-time',folder)
SOURCE.write_bytes(original)
for kind in ('corrupt','truncated','wrong-size','missing-jpg','missing-receipt','broken-receipt','unknown-temp','unknown-file','target-directory','missing-manifest','broken-manifest'):
    folder=clone('case-'+kind);jpg=next(folder.glob('*.jpg'));receipt=Path(str(jpg)+'.done.json')
    if kind=='corrupt':jpg.write_bytes(b'not a JPEG')
    elif kind=='truncated':jpg.write_bytes(jpg.read_bytes()[:-30])
    elif kind=='wrong-size':Image.new('RGB',(12,12)).save(jpg)
    elif kind=='missing-jpg':jpg.unlink()
    elif kind=='missing-receipt':receipt.unlink()
    elif kind=='broken-receipt':receipt.write_bytes(b'{')
    elif kind=='unknown-temp':(folder/'.pdf2jpg-crashed.tmp').write_bytes(b'partial')
    elif kind=='unknown-file':(folder/'other.txt').write_text('preserve')
    elif kind=='target-directory':jpg.unlink();jpg.mkdir()
    elif kind=='missing-manifest':(folder/'.pdf2jpg-batch.json').unlink()
    elif kind=='broken-manifest':(folder/'.pdf2jpg-batch.json').write_bytes(b'{')
    rejected(kind,folder)
for key in ('outputContract','qt','qtPdfSha256'):
    folder=clone('identity-'+key);manifest=folder/'.pdf2jpg-batch.json'
    meta=json.loads(manifest.read_text(encoding='utf8'))
    meta[key] = ('aa0e0c688d5991b0b075d241d39901b15a641d3b9c4e4b1f82f4b391b5ed6447'
                 if key == 'qtPdfSha256' else 'changed')
    manifest.write_text(json.dumps(meta))
    rejected('identity-'+key,folder)
folder = clone('legacy-v1.1.0')
manifest = folder/'.pdf2jpg-batch.json'
meta = json.loads(manifest.read_text(encoding='utf8'))
meta['schema'] = 1
meta.pop('qtPdfSha256')
meta['outputContract'] = 'qt-pdf-default/ceil/rgb32-white/jpeg-optimized-false/dpm-v1'
manifest.write_text(json.dumps(meta))
r = rejected('legacy-v1.1.0', folder)
assert '續傳批次格式不相容' in ' '.join(r['files'][0]['errors'])
# Exercise the decoder even when receipt hash and expected dimensions have been supplied.
folder=clone('decode-check');jpg=next(folder.glob('*.jpg'));receipt=Path(str(jpg)+'.done.json')
jpg.write_bytes(b'not jpeg');meta=json.loads(receipt.read_text(encoding='utf8'));meta['sha256']=hashlib.sha256(jpg.read_bytes()).hexdigest()
receipt.write_text(json.dumps(meta));rejected('decode-invalid',folder)
folder=clone('decode-size');jpg=next(folder.glob('*.jpg'));receipt=Path(str(jpg)+'.done.json')
Image.new('RGB',(12,12)).save(jpg);meta=json.loads(receipt.read_text(encoding='utf8'));meta['sha256']=hashlib.sha256(jpg.read_bytes()).hexdigest()
receipt.write_text(json.dumps(meta));rejected('decoded-size-invalid',folder)
# Missing both files is pending work, not a completed page.
folder=clone('pending');jpg=next(folder.glob('*.jpg'));jpg.unlink();Path(str(jpg)+'.done.json').unlink()
r=run('pending-page',resume=True,resumeFolder=str(folder));assert ok(r) and r['saved']==1 and r['skipped']==2

LARGE=WORK/'取消 再開.pdf';pdf(LARGE,100)
r=run('cancel',[LARGE],dpi=600,cancelMs=500)
assert r['cancelRequested'] and 0<r['saved']<100,r
partial=Path(r['files'][0]['folder']);old={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in partial.glob('*.jpg')}
r=run('restart',[LARGE],dpi=600,resume=True,resumeFolder=str(partial))
assert ok(r) and r['skipped']==len(old) and r['saved']+r['skipped']==100
assert all(hashlib.sha256((partial/n).read_bytes()).hexdigest()==h for n,h in old.items())

# Real process termination after atomic JPEG publication, before its receipt.
p,c=spawn('crash',[SOURCE],envs={'PDF2JPG_CRASH_AFTER_PUBLISH':'1'},output=str(WORK/'crash-output'))
assert p.wait(timeout=20)==91
crashfolder=next((WORK/'crash-output').iterdir())
assert list(crashfolder.glob('*.jpg')) and not list(crashfolder.glob('*.done.json'))
rejected('crash-orphan',crashfolder)

# Hold a real OS lock long enough to deterministically overlap a second process.
folder=clone('parallel')
p,c=spawn('lock-owner',[SOURCE],resume=True,resumeFolder=str(folder),envs={'PDF2JPG_HOLD_LOCK':'1'})
kernel=ctypes.WinDLL('kernel32',use_last_error=True)
kernel.CreateFileW.restype=ctypes.c_void_p
kernel.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_void_p]
kernel.CloseHandle.argtypes=[ctypes.c_void_p]
def held(path):
    h=kernel.CreateFileW(str(path),0x80000000,7,None,3,0,None)
    if h==ctypes.c_void_p(-1).value:return ctypes.get_last_error()==32
    kernel.CloseHandle(h);return False
deadline=time.monotonic()+10
while not held(folder/'.pdf2jpg.lock'):
    assert time.monotonic()<deadline and p.poll() is None
    time.sleep(.01)
try:
    with SOURCE.open('r+b') as f:f.write(b'BAD')
    raise AssertionError('source write was allowed')
except PermissionError:pass
rejected('parallel-lock',folder)
assert ok(finish('lock-owner',p,c))
r=run('lock-released',resume=True,resumeFolder=str(folder));assert ok(r)

# Terminate a process while it holds only the lock: Windows releases ownership after death.
p,c=spawn('kill-lock',[SOURCE],resume=True,resumeFolder=str(folder),envs={'PDF2JPG_HOLD_LOCK':'1'})
deadline=time.monotonic()+10
while not held(folder/'.pdf2jpg.lock'):
    assert time.monotonic()<deadline;time.sleep(.01)
p.kill();p.wait(timeout=10)
r=run('dead-owner-restart',resume=True,resumeFolder=str(folder));assert ok(r) and r['skipped']==3

for hook in ('THREAD','SUBMIT','WRITE','CALLBACK','RECEIPT','FINAL'):
    r=run('exception-'+hook,envs={'PDF2JPG_FAIL_'+hook:('2' if hook=='THREAD' else '1')})
    assert failed(r) or r.get('cancelRequested'),r
    if hook=='RECEIPT':
        f=Path(r['files'][0]['folder']);rejected('receipt-exception-restart',f)
    elif hook in ('THREAD','SUBMIT','WRITE'):
        f=Path(r['files'][0]['folder'])
        assert not list(f.glob('*.tmp'))
        again=run('retry-'+hook,resume=True,resumeFolder=str(f));assert ok(again)

r=run('gui-default',mode='--smoke');assert ok(r) and not r['resumeChecked'] and r['resumeEnabled'] and not r['chooseBatchEnabled']
r=run('gui-resume',mode='--smoke',resume=True,resumeFolder=str(BATCH))
assert ok(r) and r['resumeChecked'] and r['chooseBatchEnabled'] and r['skipped']==3

# A3 at 600 DPI exceeds the ordinary 256 MiB Qt decode limit.
a3 = WORK/'A3.pdf'
pdf(a3, pages=1, width=297*72/25.4, height=420*72/25.4)
r = run('a3-600-convert', [a3], dpi=600, memoryMiB=1024)
assert ok(r) and r['saved'] == 1
a3_folder = Path(r['files'][0]['folder'])
with Image.open(next(a3_folder.glob('*.jpg'))) as jpg:
    jpg.load()
    assert jpg.size == (7016, 9922)
    assert jpg.mode == 'RGB' and jpg.info['dpi'] == (600, 600)
a3_before = snap(a3_folder)
r = run('a3-600-resume', [a3], resume=True, resumeFolder=str(a3_folder), dpi=600, memoryMiB=1024)
assert ok(r) and r['saved'] == 0 and r['skipped'] == 1
assert snap(a3_folder) == a3_before
r = rejected('a3-600-budget', a3_folder, files=[a3], dpi=600, memoryMiB=768)
assert '驗證 JPG 所需影像預算不足' in ' '.join(r['files'][0]['errors'])

# The selected batch may not itself be a directory reparse point.
junction = WORK/'batch-junction'
shell = Path(os.environ['SystemRoot'])/'System32'/'cmd.exe'
subprocess.run([str(shell), '/d', '/c', 'mklink', '/J', str(junction), str(BATCH)],
               check=True, env=ENV, stdout=subprocess.DEVNULL)
rejected('batch-junction', junction)
symbolic = WORK/'batch-symlink'
try:
    symbolic.symlink_to(BATCH, target_is_directory=True)
except OSError as error:
    if error.winerror != 1314:
        raise
    print('SKIP batch-symlink: Windows does not grant symlink creation', flush=True)
else:
    rejected('batch-symlink', symbolic)

# Unicode source and selected batch both beyond the ordinary Windows MAX_PATH.
longdir=WORK
while len(str(longdir))<290:longdir=longdir/('長路徑日本語'+str(len(str(longdir))))
longdir.mkdir(parents=True);longpdf=longdir/'續傳.pdf';pdf(longpdf,1)
r=run('longpath-first',[longpdf],output=str(longdir/'輸出'));assert ok(r)
r=run('longpath-resume',[longpdf],resume=True,resumeFolder=r['files'][0]['folder'])
assert ok(r) and r['skipped']==1
(WORK/'summary.json').write_text(json.dumps(dict(passed=True,checks=checks),ensure_ascii=False,indent=2),encoding='utf8')
print('PASS',len(checks),WORK,flush=True)


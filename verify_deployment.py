"""Check production deployment with a clean PATH. No user settings are saved."""
from pathlib import Path
import ctypes, json, os, re, subprocess, time
root=Path(__file__).resolve().parent
env=os.environ.copy();env['PATH']=str(Path(os.environ['SystemRoot'])/'System32')
for k in list(env):
    if k.startswith(('QT_','QML')):env.pop(k,None)
startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
exe=root/'dist'/'PDF-to-JPG_QT.exe'
for doc in ('LICENSE','THIRD_PARTY_NOTICES.md','licenses/LGPL-3.0.txt','licenses/GPL-3.0.txt','licenses/Qt-LICENSE.txt'):
    assert (exe.parent/doc).is_file(),f'missing license file: {doc}'
# Windows SDK redistributables are not shipped (their license needs recipient terms); Windows provides d3dcompiler_47.
for sdk in ('d3dcompiler_47.dll','dxcompiler.dll','dxil.dll'):
    assert not (exe.parent/sdk).exists(),f'Windows SDK DLL deployed: {sdk}'
for stray in ('COPYING.txt','Copyright.txt','LICENSE.FDL'):
    assert not (exe.parent/'licenses'/stray).exists(),f'unrelated license file deployed: {stray}'
# SPDX templates use placeholder copyright lines; shipped license files must carry the real ones.
# Allowed: instruction lines that are part of the official license wording itself
# (Apache-2.0 appendix, FreeType credit example, GPL "How to Apply").
placeholder=re.compile(r'<year>|<yyyy|<owner>|<copyright holders?>|<name of author>|<organization>|\[yyyy\]|\[name of copyright owner\]|\[year\]|\[fullname\]',re.I)
official=re.compile(r'^(Copyright \[yyyy\] \[name of copyright owner\]'
    r'|Portions of this software are copyright © <year> The FreeType'
    r'|Please replace <year> with the value from the FreeType version you'
    r'|(<program>\s+)?Copyright \(C\) <year>\s+<name of author>)')
for folder in (root/'licenses',exe.parent/'licenses'):
    for f in folder.glob('*.txt'):
        for n,line in enumerate(f.read_text(encoding='utf8').splitlines(),1):
            text=line.strip().strip('"').strip()
            assert not placeholder.search(text) or official.match(text),f'placeholder in {f}:{n}: {text}'
notices=(root/'THIRD_PARTY_NOTICES.md').read_text(encoding='utf8')
for name in sorted(set(re.findall(r'`(?:licenses/)?([\w.-]+\.txt)`',notices))):
    for folder in (root/'licenses',exe.parent/'licenses'):
        assert (folder/name).is_file(),f'license file named in notices missing: {folder/name}'
src=sorted(f.name for f in (root/'licenses').glob('*'));dst=sorted(f.name for f in (exe.parent/'licenses').glob('*'))
assert src==dst,'dist/licenses differs from licenses/'
for f in (root/'licenses').glob('*'):
    assert f.read_bytes()==(exe.parent/'licenses'/f.name).read_bytes(),f'dist/licenses/{f.name} is stale'
p=subprocess.Popen([str(exe)],cwd=exe.parent,env=env,startupinfo=startup)
modules=set()
try:
    for _ in range(30):
        assert p.poll() is None, 'production app exited during startup'
        arr=(ctypes.c_void_p*1024)();needed=ctypes.c_ulong()
        if ctypes.windll.psapi.EnumProcessModulesEx(ctypes.c_void_p(int(p._handle)),arr,ctypes.sizeof(arr),ctypes.byref(needed),3):
            for mod in arr[:needed.value//ctypes.sizeof(ctypes.c_void_p)]:
                buf=ctypes.create_unicode_buffer(32768)
                if ctypes.windll.psapi.GetModuleFileNameExW(ctypes.c_void_p(int(p._handle)),ctypes.c_void_p(mod),buf,len(buf)):modules.add(buf.value)
        if any(Path(m).name.lower()=='qwindows.dll' for m in modules):break
        time.sleep(.1)
    for dll in ('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Pdf.dll','msvcp140.dll','qwindows.dll'):
        assert any(Path(m).name.lower()==dll.lower() and str(exe.parent).lower() in m.lower() for m in modules),dll
    result=dict(passed=True,exe=str(exe),loadedModules=sorted(modules),note='Idle startup verified; no conversion active when test process was terminated.')
    (root/'work'/'release-deployment.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print('PASS production deployment: Qt/PDF/Windows plugin/MSVC CRT loaded from dist')
finally:
    p.terminate();p.wait(timeout=10)

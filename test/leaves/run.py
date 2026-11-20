"""Hermetic CLI / parser / runtime regression tests; run from any directory."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
LEAV = str(ROOT / 'src/leav')
LEAVC = str(ROOT / 'src/leavc')
count = 0


def run(args, ok=True, contains=None):
    global count
    p = subprocess.run(args, text=True, capture_output=True, cwd=ROOT)
    assert (p.returncode == 0) == ok, (args, p.stdout, p.stderr)
    if contains:
        assert contains in p.stdout, (args, p.stdout, p.stderr)
    count += 1


run([LEAV, str(ROOT / 'test/leaves/objects.leav')], contains='objects OK')
run([LEAV, '-e', 'local class, extends = 1, 2; assert(class+extends==3)'])
run([LEAV, '--lua', '-e', 'local class = 1; assert(class==1)'])
run([LEAV, '--leaves', '-e', 'class A end; assert(A():isa(A))'])
run([LEAV, '-L', '-e', 'class A end; assert(A():isa(A))'])
run([LEAV, '--lua', str(ROOT / 'test/leaves/objects.leav')], ok=False)
run([LEAV, '--leaves', '-e', 'class Bad extends {} end'], ok=False)
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    plain = tmp / 'plain.leav'
    plain.write_text('local class = 9; assert(class == 9)')
    run([LEAV, '--lua', str(plain)])
    run([LEAV, str(plain)], ok=False)
    code = tmp / 'objects.luac'
    run([LEAVC, '-o', str(code), str(ROOT / 'test/leaves/objects.leav')])
    run([LEAV, str(code)], contains='objects OK')
    run([LEAVC, '--lua', '-p', str(ROOT / 'test/leaves/objects.leav')], ok=False)
    forced = tmp / 'forced.lua'
    forced.write_text('class A end; assert(A():isa(A))')
    run([LEAV, str(forced)], ok=False)
    run([LEAV, '--leaves', str(forced)])
    run([LEAVC, '--leaves', '-p', str(forced)])
print(f'{count} regression checks passed')

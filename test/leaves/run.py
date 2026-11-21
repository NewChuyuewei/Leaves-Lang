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
    # BND-006: a hung process or signal termination is never an expected error.
    p = subprocess.run(args, text=True, capture_output=True, cwd=ROOT, timeout=30)
    if p.returncode < 0:
        raise AssertionError(('process terminated by signal', args, p.stderr))
    if (p.returncode == 0) != ok:
        raise AssertionError((args, p.stdout, p.stderr))
    if contains and contains not in p.stdout:
        raise AssertionError((args, p.stdout, p.stderr))
    count += 1


run([LEAV, str(ROOT / 'test/leaves/objects.leav')], contains='objects OK')
for name, message in [('updates', 'method updates OK'),
                      ('coroutines', 'constructor coroutines OK'),
                      ('parents', 'parent expressions OK'),
                      ('bounded', 'bounded lookup OK')]:
    run([LEAV, str(ROOT / f'test/leaves/{name}.leav')], contains=message)
run([LEAV, str(ROOT / 'test/leaves/constructors.leav')], contains='constructors OK')
run([LEAV, str(ROOT / 'test/leaves/locals.leav')], contains='class locals OK')
run([LEAV, '--lua', str(ROOT / 'test/leaves/locals.leav')], ok=False)
run([LEAV, '--leaves', '-e',
     'class A local x <const> = 1; function set() x = 2 end end'], ok=False)
run([LEAV, '--leaves', '-e', 'class A local x = end'], ok=False)
run([LEAV, '--leaves', '-e', 'class A local function f() end'], ok=False)
run([LEAV, '--leaves', '-e', 'class A return 1 end'], ok=False)
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
    locals_code = tmp / 'locals.luac'
    run([LEAVC, '-o', str(locals_code), str(ROOT / 'test/leaves/locals.leav')])
    run([LEAV, str(locals_code)], contains='class locals OK')
    stripped = tmp / 'stripped.luac'
    run([LEAVC, '-s', '-o', str(stripped), str(ROOT / 'test/leaves/locals.leav')])
    run([LEAV, str(stripped)], contains='class locals OK')
    constructors = tmp / 'constructors.luac'
    run([LEAVC, '-s', '-o', str(constructors),
         str(ROOT / 'test/leaves/constructors.leav')])
    run([LEAV, str(constructors)], contains='constructors OK')
    for name, message in [('updates', 'method updates OK'),
                          ('coroutines', 'constructor coroutines OK'),
                          ('parents', 'parent expressions OK'),
                          ('bounded', 'bounded lookup OK')]:
        for strip in (False, True):
            output = tmp / f'{name}-{strip}.luac'
            run([LEAVC, *(['-s'] if strip else []), '-o', str(output),
                 str(ROOT / f'test/leaves/{name}.leav')])
            run([LEAV, str(output)], contains=message)
    run([LEAV, '--lua', '-e',
         'local C = leaves.class("C"); '
         'leaves.setmethod(C, "get", function(self) return 7 end); '
         'assert(C():get() == 7)'])
    forced = tmp / 'forced.lua'
    forced.write_text('class A end; assert(A():isa(A))')
    run([LEAV, str(forced)], ok=False)
    run([LEAV, '--leaves', str(forced)])
    run([LEAVC, '--leaves', '-p', str(forced)])
print(f'{count} regression checks passed')

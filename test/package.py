"""Package an already-built Leaves tree and smoke-test its extracted archive."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args, cwd=ROOT):
    result = subprocess.run(args, cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=120)
    if result.returncode != 0:
        print(result.stdout, end='')
        result.check_returncode()
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', required=True)
    parser.add_argument('--platform', required=True)
    parser.add_argument('--flags', default='')
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z0-9-]+', args.configuration):
        parser.error('configuration must contain only lowercase letters, digits or hyphens')

    work = ROOT / '.work'
    work.mkdir(exist_ok=True)
    output = work / 'artifacts'
    output.mkdir(exist_ok=True)
    windows = os.name == 'nt'
    suffix = '.exe' if windows else ''
    extension = '.zip' if windows else '.tar.gz'
    name = f'leaves-{args.configuration}-{platform.machine()}'
    archive = output / f'{name}{extension}'
    # Never leave an old successful archive behind if this invocation fails.
    archive.unlink(missing_ok=True)
    checksum = output / f'{name}{extension}.sha256'
    checksum.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix='package-', dir=work) as temporary:
        staging = Path(temporary)
        package = staging / name
        if windows:
            for directory in ('bin', 'lib', 'include'):
                (package / directory).mkdir(parents=True, exist_ok=True)
            for executable in ('leav.exe', 'leavc.exe'):
                shutil.copy2(ROOT / 'src' / executable, package / 'bin' / executable)
            shutil.copy2(ROOT / 'src/liblua.a', package / 'lib/liblua.a')
            for header in ('lua.h', 'luaconf.h', 'lauxlib.h', 'lualib.h', 'lua.hpp'):
                shutil.copy2(ROOT / 'src' / header, package / 'include' / header)
        else:
            print(run('make', 'install', f'INSTALL_TOP={package}'), end='')
        for filename in ('README', 'ROADMAP.md'):
            shutil.copy2(ROOT / filename, package / filename)
        for directory in ('examples', 'doc'):
            shutil.copytree(ROOT / directory, package / directory)
        info = {
            'commit': run('git', 'rev-parse', 'HEAD').strip(),
            'working_tree_dirty': bool(run('git', 'status', '--porcelain').strip()),
            'configuration': args.configuration,
            'make_platform': args.platform,
            'extra_cflags': args.flags,
            'system': platform.platform(),
            'architecture': platform.machine(),
            'compiler': run('gcc', '--version').splitlines()[0],
            'python': platform.python_version(),
            'note': 'CI build; not a signed release or a certification claim. '
                    'Platform libraries are not bundled. Compiler/flags describe '
                    'the documented CI invocation, not an inspection of object files.',
        }
        (package / 'BUILD-INFO.json').write_text(
            json.dumps(info, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        for relative in (f'bin/leav{suffix}', f'bin/leavc{suffix}', 'lib/liblua.a',
                         'include/lua.h', 'include/lauxlib.h', 'BUILD-INFO.json'):
            if not (package / relative).is_file():
                raise RuntimeError(f'missing package file: {relative}')

        pending = staging / f'{name}{extension}'
        if windows:
            with zipfile.ZipFile(pending, 'w', zipfile.ZIP_DEFLATED) as bundle:
                for item in sorted(package.rglob('*')):
                    if item.is_file():
                        bundle.write(item, item.relative_to(staging))
        else:
            with tarfile.open(pending, 'w:gz') as bundle:
                bundle.add(package, arcname=name)
        extracted = staging / 'extracted'
        extracted.mkdir()
        # This archive was created immediately above from our staging tree;
        # no untrusted downloaded archives are accepted by this script.
        if windows:
            with zipfile.ZipFile(pending) as bundle:
                bundle.extractall(extracted)
        else:
            run('tar', '-xzf', str(pending), '-C', str(extracted))
        installed = extracted / name
        interpreter = str(installed / f'bin/leav{suffix}')
        compiler = str(installed / f'bin/leavc{suffix}')
        print(run(interpreter, '-v', cwd=installed), end='')
        for example in ('shapes.leav', 'wallet.leav'):
            print(run(interpreter, str(installed / 'examples' / example),
                      cwd=installed), end='')
        bytecode = staging / 'shapes.luac'
        run(compiler, '-o', str(bytecode),
            str(installed / 'examples/shapes.leav'), cwd=installed)
        run(interpreter, str(bytecode), cwd=installed)
        # Publish locally only after extraction and execution have succeeded.
        shutil.move(str(pending), archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksum.write_text(f'{digest}  {archive.name}\n', encoding='utf-8')
    print(f'Archive validated: {archive.relative_to(ROOT)}')


if __name__ == '__main__':
    main()

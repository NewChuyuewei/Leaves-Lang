"""Compare two identically configured leav builds, alternating run order."""
import argparse
from pathlib import Path
import platform
import statistics
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('before', type=Path)
parser.add_argument('after', type=Path)
parser.add_argument('--runs', type=int, default=9)
parser.add_argument('--iterations', type=int, default=200000)
args = parser.parse_args()
if args.runs < 3 or args.iterations < 1:
    parser.error('use at least 3 runs and positive iterations')
programs = [args.before.resolve(), args.after.resolve()]
workload = Path(__file__).with_name('runtime.leav')


def measure(program):
    result = subprocess.run(
        [str(program), str(workload), str(args.iterations)],
        check=True, text=True, capture_output=True)
    rows = [line.split() for line in result.stdout.splitlines()]
    return {name: float(seconds) for name, seconds in rows}


# Warm up both executables; each subsequent sample uses a fresh process.
for program in programs:
    measure(program)
samples = [{}, {}]
for i in range(args.runs):
    for which in ([0, 1] if i % 2 == 0 else [1, 0]):
        for name, seconds in measure(programs[which]).items():
            samples[which].setdefault(name, []).append(seconds)
assert samples[0].keys() == samples[1].keys()
print(platform.platform())
print(f'runs={args.runs}, iterations={args.iterations}, CPU time via os.clock')
print(f'before={programs[0]}\nafter={programs[1]}')
print(f'{"workload":24} {"before ms":>12} {"after ms":>12} {"change":>10}')
for name in samples[0]:
    before = statistics.median(samples[0][name])
    after = statistics.median(samples[1][name])
    change = f'{(after / before - 1) * 100:+.1f}%' if before else 'n/a'
    print(f'{name:24} {before * 1000:12.3f} {after * 1000:12.3f} {change:>10}')

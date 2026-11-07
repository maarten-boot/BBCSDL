#!/usr/bin/env python3
"""Run the shared BBC BASIC test corpus on a BBC BASIC build.

    bastest.py [-e EXE] [-o OUTDIR] [-r REFDIR] [-t SECS] [-j JOBS] [TEST ...]

The corpus is bbc-basic-68k's (../bbc-basic-68k/src68k/test/{bas,kw,guide}/*.in,
format described in its tools/bascheck.py): lines typed at the '>' prompt,
"#file PATH" puts PATH beside the test, "#typed" tests are skipped here.

Each test runs in a fresh temporary folder as  NEW <lines> *BYE  typed into
EXE (default: the Linux reference build, build/host/bbcbasic), each line
after the prompt ('>' or '?') is back or the output has been idle for IDLE
seconds: the console edition loses keys when its 256-byte keyboard queue
overflows, so a test cannot simply be piped in.  The output,
with the banner, the prompt/echo lines ('>') and control characters removed,
is written to OUTDIR/<group>/<name>.out.  With -r REFDIR the outputs are
compared with REFDIR/<group>/<name>.out and the differing tests listed
(exit status 1 if any differ).  -j runs JOBS tests at once (the Linux
console edition types about 7 ms a character, so a long test takes minutes).
"""

import argparse
import glob
import os
import re
import select
import shutil
import time
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
CORPUS = os.path.normpath(os.path.join(TOP, '..', 'bbc-basic-68k', 'src68k', 'test'))
GROUPS = ('bas', 'kw', 'guide')
WIDTH = 255     # the pseudo-terminal's width, as reported to ESC[6n


def parse(test):
    lines, files, typed = [], [], False
    with open(test, 'rb') as f:
        for l in f.read().decode('latin-1').split('\n'):
            if l.startswith('#file '):
                files.append(os.path.normpath(os.path.join(os.path.dirname(test), l[6:].strip())))
            elif l.rstrip() == '#typed':
                typed = True
            else:
                lines.append(l.rstrip('\r'))
    while lines and lines[-1] == '':
        lines.pop()
    return lines, files, typed


def clean(raw):
    text = re.sub(r'\x1b\[[0-9;?]*[A-Za-z]', '', raw.decode('latin-1')).replace('\r', '')
    text = re.sub(r'[\x00-\x08\x0b-\x1f\x7f]', '', text)
    out, banner = [], 0
    for l in text.split('\n'):
        if banner < 2 and (l.startswith('BBC BASIC') or l.startswith('(C) Copyright')):
            banner += 1
            continue
        if l.startswith('>'):
            continue
        out.append(l)
    while out and out[-1] == '':
        out.pop()
    return '\n'.join(out) + '\n'


def typein(exe, cwd, lines, secs, idle):
    """Type the lines into exe, each when the prompt is back; the output.

    stdin is a pipe, so the console edition does not query the cursor
    position (ESC[6n) or switch the terminal to raw mode; stdout is a
    pseudo-terminal, so its output is line-buffered rather than held in
    a 4 KB stdio buffer."""
    import pty
    master, slave = pty.openpty()
    p = subprocess.Popen([exe], cwd=cwd, stdin=subprocess.PIPE,
                         stdout=slave, stderr=slave, close_fds=True)
    os.close(slave)
    out = bytearray()
    end = time.time() + secs
    todo = list(lines)
    last = time.time()
    done = False
    while time.time() < end:
        r, _, _ = select.select([master], [], [], 0.01)
        if r:
            try:
                b = os.read(master, 65536)
            except OSError:
                b = b''
            if not b:
                done = True
                break
            out += b
            last = time.time()
            continue
        tail = re.sub(rb'[\x00-\x1f\x7f]', b'', bytes(out[-64:]))
        ready = tail.endswith(b'>') or tail.endswith(b'?') or time.time() - last > idle
        if todo and ready:
            try:
                p.stdin.write((todo.pop(0) + '\n').encode('latin-1'))
                p.stdin.flush()
            except BrokenPipeError:
                pass
            last = time.time()
        elif not todo and p.poll() is not None:
            done = True
            break
    if not done:
        out += b'\n(timeout)\n'
    p.kill()
    p.wait()
    os.close(master)
    return bytes(out)


def run(exe, test, secs, idle=1.0):
    lines, files, typed = parse(test)
    if typed:
        return None
    with tempfile.TemporaryDirectory() as d:
        for f in files:
            shutil.copy(f, os.path.join(d, os.path.basename(f)))
        raw = typein(exe, d, ['NEW'] + lines + ['*BYE'], secs, idle)
    return clean(raw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-e', default=os.path.join(TOP, 'build', 'host', 'bbcbasic'))
    ap.add_argument('-o', default=os.path.join(TOP, 'build', 'bas', 'host'))
    ap.add_argument('-r')
    ap.add_argument('-t', type=int, default=600)
    ap.add_argument('-j', type=int, default=os.cpu_count() or 1)
    ap.add_argument('tests', nargs='*')
    a = ap.parse_args()
    exe = os.path.abspath(a.e)
    tests = a.tests or sorted(t for g in GROUPS for t in glob.glob(os.path.join(CORPUS, g, '*.in')))
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(a.j) as ex:
        outs = list(ex.map(lambda t: run(exe, t, a.t), tests))
    differ, skipped = [], 0
    for t, out in zip(tests, outs):
        g = os.path.basename(os.path.dirname(t))
        n = os.path.basename(t)[:-3]
        if out is None:
            skipped += 1
            continue
        os.makedirs(os.path.join(a.o, g), exist_ok=True)
        with open(os.path.join(a.o, g, n + '.out'), 'w', encoding='latin-1') as f:
            f.write(out)
        if a.r:
            rp = os.path.join(a.r, g, n + '.out')
            ref = open(rp, encoding='latin-1').read() if os.path.exists(rp) else None
            if ref != out:
                differ.append(f'{g}/{n}')
    print(f'{len(tests) - skipped} run, {skipped} skipped (#typed)', end='')
    if a.r:
        print(f', {len(differ)} differ' + ''.join('\n  ' + d for d in differ))
    else:
        print()
    return 1 if differ else 0


if __name__ == '__main__':
    sys.exit(main())

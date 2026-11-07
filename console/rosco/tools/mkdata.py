#!/usr/bin/env python3
"""Make the data module for a BBC_LEMEM build from src/bbdata_arm_32.s.

    mkdata.py SRC.s OUT.s

The ARM data module is plain GNU as data directives, which the m68k and the
i386 assemblers accept too.  In BBC_LEMEM builds (include/bbcplat.h) the
tables that BASIC reads through the heap macros must be little-endian in
memory: the @ system-variable list (from 'sysvar:'), the @vdu{} format list,
the {x%,y%} and {a&,b&,c&,d&} formats and the @fn%() array.  An assembler
writes .long in the CPU's byte order, and an address constant cannot be
byte-swapped when assembling, so every .long in that region gets a label
and is listed in bbdata_lefix_tab; lefix.c reverses each of them, byte by
byte, once at start-up.  (On the 68k that makes them little-endian; in the
PC test build, BBC_SWAPTEST, it makes them reversed as the macros expect.)
"""

import re
import sys

START = 'sysvar:'          # first line of the region (a label)
END = re.compile(r'^\s*\.align')   # the fonts that follow are byte data


def main(src, out):
    lines = open(src, encoding='latin-1').read().split('\n')
    res, labels, inside = [], [], False
    for l in lines:
        code = l.split('/*')[0]
        if code.strip().startswith(START):
            inside = True
        elif inside and END.match(code):
            inside = False
        m = re.match(r'^(\s*[A-Za-z_][A-Za-z_0-9]*:)?\s*\.long\s+(.*)$', code)
        if inside and m:
            comment = l[len(code):]
            label = m.group(1)
            if label:
                res.append(label.strip())
            for v in m.group(2).split(','):
                n = f'__lefix{len(labels)}'
                labels.append(n)
                res.append(f'{n}:\t.long\t{v.strip()}')
            if comment:
                res.append('\t' + comment.strip())
            continue
        res.append(l)
    if not labels:
        sys.exit('mkdata.py: no .long found after ' + START)
    res += ['', '/* Words to make little-endian at start-up (lefix.c) */',
            '\t.global\tbbdata_lefix_tab', '\t.global\tbbdata_lefix_cnt',
            '\t.data', '\t.balign\t4',
            'bbdata_lefix_cnt:\t.long\t%d' % len(labels),
            'bbdata_lefix_tab:']
    res += ['\t.long\t' + n for n in labels]
    open(out, 'w', encoding='latin-1').write('\n'.join(res) + '\n')


if __name__ == '__main__':
    main(*sys.argv[1:])

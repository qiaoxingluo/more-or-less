# -*- coding: utf-8 -*-
"""Sanity check: find degenerate / garbled lines in the repo markdown files.

Degeneration shows up as the same short token repeated many times in a row,
which is never legitimate in Chinese technical prose.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PATTERNS = [
    ('repeat-token', re.compile(r'(?:[\u4e00-\u9fff]|[A-Za-z]{1,4})()()()()()\1{6,}')),
]

# simpler + robust: repeated identical 2-char CJK or identical word >= 6 times
REPEAT = re.compile(r'((?:[\u4e00-\u9fff]|[\u3000-\u303f]|[A-Za-z]+)[]]?)()\1{5,}')

SUSPECT_WORDS = [
    'punish', 'FigureTrax', ' codesamples', 'claimed 手法', 'SortTests',
]

def scan(path):
    problems = []
    with io.open(path, encoding='utf-8') as fp:
        for lineno, line in enumerate(fp, 1):
            line = line.rstrip('\n')
            if len(line) > 4000:
                problems.append((lineno, 'line too long (%d chars)' % len(line)))
            m = REPEAT.search(line)
            if m:
                problems.append((lineno, 'repeat: %r' % m.group(0)[:40]))
            for w in SUSPECT_WORDS:
                if w in line:
                    problems.append((lineno, 'suspect word: %s' % w))
    return problems


def main():
    total = 0
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in ('.git', 'node_modules')]
        for name in sorted(filenames):
            if not name.lower().endswith(('.md', '.py')):
                continue
            path = os.path.join(dirpath, name)
            # Skip the scanner itself: its SUSPECT_WORDS list legitimately
            # contains the garbled tokens it is looking for.
            if os.path.abspath(path) == os.path.abspath(__file__):
                continue
            problems = scan(path)
            if problems:
                rel = os.path.relpath(path, ROOT)
                for lineno, msg in problems[:10]:
                    print('%s:%d  %s' % (rel, lineno, msg))
                total += len(problems)
    if total == 0:
        print('OK: no garbled lines detected')
    else:
        print('\nTotal problems: %d' % total)
    return 0 if total == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

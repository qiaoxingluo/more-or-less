# -*- coding: utf-8 -*-
"""Dump FuncList / Limits from every .tmf under a directory tree.

Two traps handled here (see SKILL.md section 5.1):
  1. FuncList lives at SiteList[0].FuncList[], NOT at top level.
  2. .tmf files carry a BOM -> must be opened with encoding='utf-8-sig'.
"""
import io
import json
import os
import sys


def walk_tmf(root):
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in sorted(filenames):
            if name.lower().endswith('.tmf'):
                yield os.path.join(dirpath, name)


def dump_one(path, verbose=False):
    with io.open(path, encoding='utf-8-sig') as fp:
        data = json.load(fp)

    print('=' * 72)
    print(os.path.basename(path))
    print('  Version=%s  Date=%s' % (data.get('Version'), data.get('ChangeDate')))
    print('  ProjectInit=%s  Reset=%s  Reuse=%s'
          % (data.get('ProjectInitName'), data.get('ProjectResetName'),
             data.get('ProjectReuseName')))

    # Trap #1: FuncList is per-site, not top level.
    for site in data.get('SiteList', []):
        print('  Site %s (Loop=%s, ErrorSbin=%s, ErrorHbin=%s)'
              % (site.get('SiteNumber'), site.get('LoopCount'),
                 site.get('ErrorSbin'), site.get('ErrorHbin')))
        for func in site.get('FuncList', []):
            params = func.get('ResultAnalysis') or []
            print('    FN%-3s %-22s Sbin=%-4s Hbin=%-4s n=%s'
                  % (func.get('FuncNumber'), func.get('FuncName'),
                     func.get('Sbin'), func.get('Hbin'), len(params)))
            if params:
                names = [p.get('ParamName') for p in params]
                print('         params: %s' % ', '.join(names))
                if verbose:
                    for p in params:
                        print('         %-6s %s ~ %s %s'
                              % (p.get('ParamName'), p.get('Min'),
                                 p.get('Max'), p.get('Unit')))


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    verbose = '-v' in sys.argv
    found = False
    for tmf in walk_tmf(root):
        dump_one(tmf, verbose)
        found = True
    if not found:
        print('No .tmf found under %s' % root)


if __name__ == '__main__':
    main()

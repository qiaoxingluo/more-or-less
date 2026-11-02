# -*- coding: utf-8 -*-
"""SMU_AWG_Demo 工程一致性校验

跨文件交叉检查（专治改漏）：
  1. .tmf 的 FuncName 必须在 Test.cpp 中有 USER_CODE void <name>()
  2. Test.cpp 里 Signal("X") 的 X 必须在 .sig 或 .grp 中定义
  3. .tmf 每个测试项的 ResultAnalysis 条数必须等于 Test.cpp 里 CheckResultAndBin 的计数值
  4. Test.cpp 里引用的 pattern 名必须在 .pat 中登记
  5. 每个 pattern csv 的列必须等于 CTRL_PINS + OUTPUT_PINS
  6. 括号平衡
"""
import io
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
NAME = 'SMU_AWG_Demo'


def read(p):
    with io.open(p, encoding='utf-8-sig') as fp:
        return fp.read()


def parse_grp(p):
    g = {}
    for line in read(p).splitlines():
        line = line.strip()
        if not line or line.lower().startswith('grpname'):
            continue
        parts = [x.strip().strip('"') for x in line.split(',') if x.strip()]
        if len(parts) >= 2:
            g[parts[0]] = parts[1:]
    return g


def parse_sig(p):
    out = []
    for line in read(p).splitlines():
        line = line.strip()
        if not line or line.lower().startswith('signame'):
            continue
        parts = [x.strip().strip('"') for x in line.split(',')]
        if parts and parts[0]:
            out.append(parts[0])
    return out


def main():
    ok, err = [], []

    grp = parse_grp(os.path.join(ROOT, NAME + '.grp'))
    sig = parse_sig(os.path.join(ROOT, NAME + '.sig'))
    flist = json.loads(read(os.path.join(ROOT, NAME + '.tmf')))['SiteList'][0]['FuncList']
    cpp = read(os.path.join(ROOT, 'Test.cpp'))
    pat = read(os.path.join(ROOT, NAME + '.pat'))

    # 1) FuncName ↔ 导出函数
    exported = set(re.findall(r'USER_CODE\s+void\s+(\w+)\s*\(', cpp))
    for f in flist:
        (ok if f['FuncName'] in exported else err).append(
            'FuncName %s %s' % (f['FuncName'],
                                '有定义' if f['FuncName'] in exported else '在 Test.cpp 中缺失'))

    # 2) Signal("X")
    used = set(re.findall(r'\.Signal\("(\w+)"\)', cpp))
    known = set(sig) | set(grp)
    for s in sorted(used):
        (ok if s in known else err).append(
            'Signal("%s") %s' % (s, '已定义' if s in known else '未在 .sig/.grp 中定义'))

    # 3) 参数数 ↔ CheckResultAndBin 计数值
    counts = [int(m) for m in re.findall(r'CheckResultAndBin\(0,\s*\w+,\s*(\d+)\)', cpp)]
    tmf_counts = [len(f['ResultAnalysis']) for f in flist
                  if not f['FuncName'].startswith(('AWG_EN')) and f['FuncName'] != 'SetPassBin']
    if counts == tmf_counts:
        ok.append('CheckResultAndBin 计数 %s 与 .tmf 参数数一一对应' % counts)
    else:
        err.append('CheckResultAndBin 计数 %s 与 .tmf 参数数 %s 不一致' % (counts, tmf_counts))

    # 4) pattern 名
    pat_names = set(re.findall(r'dio\.Pattern\("(\w+)"\)', cpp))
    pat_reg = set(re.findall(r',,(\w+),\d+,true,', pat))
    for n in sorted(pat_names):
        (ok if n in pat_reg else err).append(
            'pattern "%s" %s' % (n, '已在 .pat 登记' if n in pat_reg else '未在 .pat 中登记'))

    # 5) csv 列 == CTRL_PINS + OUTPUT_PINS
    want = grp.get('CTRL_PINS', []) + grp.get('OUTPUT_PINS', [])
    for n in sorted(pat_reg):
        path = os.path.join(ROOT, 'pattern', n.lower() + '.csv')
        if not os.path.exists(path):
            err.append('pattern 文件不存在: %s' % path)
            continue
        header = read(path).splitlines()[0].strip().split(',')
        # 前 3 列为 Label,WFT,Sequence
        cols = [c for c in header[3:] if c]
        if cols == want:
            ok.append('%s.csv 列与 CTRL_PINS+OUTPUT_PINS 一致（%d 列）' % (n.lower(), len(cols)))
        else:
            err.append('%s.csv 列不匹配: 实际 %s / 期望 %s' % (n.lower(), cols, want))

    # 6) 括号平衡
    s2 = re.sub(r'/\*.*?\*/', '', cpp, flags=re.S)
    s2 = re.sub(r'//[^\n]*', '', s2)
    if s2.count('{') == s2.count('}') and s2.count('(') == s2.count(')'):
        ok.append('括号平衡 {}=%d ()=%d' % (s2.count('{'), s2.count('(')))
    else:
        err.append('括号不平衡 {}=%d/{}  ()=%d/()'
                   % (s2.count('{'), s2.count('}'), s2.count('('), s2.count(')')))

    print('工程: %s' % NAME)
    print('-' * 64)
    for x in ok:
        print('  [OK]   %s' % x)
    for x in err:
        print('  [FAIL] %s' % x)
    print('-' * 64)
    print('通过 %d 项，失败 %d 项' % (len(ok), len(err)))
    return 1 if err else 0


if __name__ == '__main__':
    sys.exit(main())

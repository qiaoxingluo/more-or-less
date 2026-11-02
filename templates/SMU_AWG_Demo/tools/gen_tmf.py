# -*- coding: utf-8 -*-
"""生成 SMU_AWG_Demo.tmf

6 个测试项：
  FN0 IDD_SMU_T          SMU 5V 供电，测静态电流 IDD
  FN1 IDD_SWEEP_SMU_T    SMU 扫 2.0/3.3/5.0/5.5V，四点各测 IDD
  FN2 VCC_IV_SMU_T       SMU 扫 0~6V，记录 VCC 脚 I-V 曲线
  FN3 AWG_EN_ON_T        AWG 给 E3 = 4.0V（使能），期望 Y0=L
  FN4 AWG_EN_OFF_T       AWG 给 E3 = 0.5V（禁能），期望全 H
  FN5 SetPassBin

坑（见仓库 references/04）：utf-8-sig 写出；FuncList 在 SiteList[0] 下。
"""
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), 'SMU_AWG_Demo.tmf')

# 暂定限值：HC138 静态电流按 50uA 卡（**待实测标定**，见方案第 5 节）
IDD_MAX = '0.00005'      # 50 uA
IV_MAX = '0.0001'        # 100 uA


def p(name, idx, lo, hi, unit):
    return {'ParamName': name, 'ParamNumber': idx,
            'Min': lo, 'Max': hi, 'Unit': unit}


def f(num, name, sbin, results, params):
    return {'FuncNumber': num, 'FuncName': name, 'FuncDesc': '',
            'LoopCount': 1, 'Sbin': sbin, 'Hbin': sbin,
            'Results': results, 'ResultAnalysis': params}


def build():
    fl = [
        f(0, 'IDD_SMU_T', 2, 'Fail', [p('IDD', 0, '0', IDD_MAX, 'A')]),
        f(1, 'IDD_SWEEP_SMU_T', 3, 'Fail', [
            p('IDD_2V0', 0, '0', IDD_MAX, 'A'),
            p('IDD_3V3', 1, '0', IDD_MAX, 'A'),
            p('IDD_5V0', 2, '0', IDD_MAX, 'A'),
            p('IDD_5V5', 3, '0', IDD_MAX, 'A'),
        ]),
        f(2, 'VCC_IV_SMU_T', 4, 'Fail', [
            p('I_VCC_0V', 0, '0', IV_MAX, 'A'),
            p('I_VCC_1V', 1, '0', IV_MAX, 'A'),
            p('I_VCC_2V', 2, '0', IV_MAX, 'A'),
            p('I_VCC_3V', 3, '0', IV_MAX, 'A'),
            p('I_VCC_4V', 4, '0', IV_MAX, 'A'),
            p('I_VCC_5V', 5, '0', IV_MAX, 'A'),
            p('I_VCC_6V', 6, '0', IV_MAX, 'A'),
        ]),
        f(3, 'AWG_EN_ON_T', 5, 'Fail', [p('AWG_EN_ON', 0, '0', '0', '')]),
        f(4, 'AWG_EN_OFF_T', 6, 'Fail', [p('AWG_EN_OFF', 0, '0', '0', '')]),
        f(5, 'SetPassBin', 1, 'Pass', [p('Param', 0, '0', '0', '')]),
    ]
    return {
        'ChangeDate': 'Mon Nov 02 16:30:00 CST 2026',
        'Version': 'V1.0',
        'ProjectInitName': 'ProjectInit',
        'ProjectResetName': 'ProjectReset',
        'ProjectReuseName': 'ProjectReuse',
        'SiteList': [{
            'SiteNumber': 0,
            'SiteInitName': 'site_init',
            'SiteResetName': 'site_reset',
            'LoopCount': 1,
            'ErrorSbin': 99, 'ErrorHbin': 99, 'BinLoopCount': 0,
            'FuncList': fl,
        }],
    }


def main():
    data = build()
    with io.open(OUT, 'w', encoding='utf-8-sig', newline='\r\n') as fp:
        json.dump(data, fp, ensure_ascii=False, indent='\t')
        fp.write('\n')
    print('已写出: %s' % OUT)

    back = json.load(io.open(OUT, encoding='utf-8-sig'))
    flist = back['SiteList'][0]['FuncList']
    assert len(flist) == 6, 'FuncList 应为 6 项'
    for f_ in flist:
        n = len(f_['ResultAnalysis'])
        assert [x['ParamNumber'] for x in f_['ResultAnalysis']] == list(range(n))
        # 限值必须 lo <= hi
        for x in f_['ResultAnalysis']:
            assert float(x['Min']) <= float(x['Max']), '%s 限值反序' % x['ParamName']
        print('  %-16s Sbin=%-3s n=%d' % (f_['FuncName'], f_['Sbin'], n))
    print('[OK] 回读校验通过')
    return 0


if __name__ == '__main__':
    sys.exit(main())

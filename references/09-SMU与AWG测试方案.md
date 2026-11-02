# SMU 与 AWG 测试方案（用仪器测 DUT）

| 项目 | 内容 |
|---|---|
| 涉及仪器 | SMU（高压电源模块）、AWG（任意波形发生器模块） |
| 测试平台 | 加速科技 ST2500 / ST2516 + SE-GIB100 |
| 被测器件 | 74HC138（DIP-16，载体；方案本身与器件无关） |
| 覆盖内容 | SMU 3 类测试项（IDD / 电压扫描 / I-V 曲线）+ AWG 2 类测试项（直流电平 / 交流激励） |
| 配套工程 | `templates/SMU_AWG_Demo/` |
| 版本 / 日期 | V1.0 / 2026-11-02 |

> 本方案的目标不是"测某一颗芯片"，而是**把 SMU 和 AWG 这两件仪器的标准用法固化成可复用的测试套路**。
> 换 DUT 时只改 `.sig` 的引脚名和限值，`Test.cpp` 的调用链基本不动。

---

## 一、术语对齐（先纠正一个高频误解）

### 1.1 ⚠️ ST2500 的 SMU 不是业界的 Source Measure Unit

平台手册原文：

> **SMU 高压电源模块，独立的 4 通道电压电源板卡** Supply
> SMU_GANG2 高压电源模块，两通道 SMUGang Supply
> SMU_GANG3 高压电源模块，三通道 SMUGang Supply
> SMU_GANG4 高压电源模块，四通道 SMUGang Supply

所以：

| | ST2500 的 SMU | 业界通用 SMU |
|---|---|---|
| 本质 | **高压电源板卡**（4 通道） | 源测量单元 |
| 强项 | 高压输出（±20 V 档）、多通道并联（GANG） | 宽动态范围的精密源测 |
| 典型用途 | 给 LCD 驱动 / PMIC 供高压 | 器件 I-V 特性、半导体参数分析 |

**但它的 API 确实具备完整的 force/measure 能力**，所以在本方案里我们按"可编程源表"来用它 —— 只是要知道它的量程和精度取向是"电源"而非"精密源表"。

### 1.2 常规 DC 参数该用谁

| 需求 | 首选 | 理由 |
|---|---|---|
| 每脚独立小电流测量（OS / Leakage） | **PPMU** | 每通道一个，32 个可并行 |
| 大电流或较高精度单点 | **BPMU** | ±80 mA，5 µA~80 mA 五档 |
| 一般 DUT 供电 | **DPS** | 4 通道，够用 |
| **高压**（>10 V）或需要 4 线/GANG | **SMU** | 唯一能到 ±20 V 的资源 |
| 模拟激励（正弦/斜坡/方波） | **AWG** | 唯一任意波形源 |

> 一句话：**能用 PPMU/DPS 解决的就别动 SMU**。SMU 的定位是"高压 + 多通道并联"，不是万金油。

---

## 二、仪器能力（全部来自 `usrInterface.h` 与平台手册）

### 2.1 SMU

| 项 | 规格 | 出处 |
|---|---|---|
| 通道数 | 4（独立）；SMU_GANG2/3/4 可并联 | 平台手册 |
| 电压档 | ±1 V / ±5 V / ±10 V / ±20 V | `ENUM_SMU_VOLT_RANGE` |
| 电流档 | FN / ±1 µA / ±10 µA / ±100 µA / ±1 mA / ±10 mA / ±100 mA / ±500 mA | `ENUM_SMU_CURR_RANGE` |
| 档位约束 | **20 V 档只能配 ≤100 mA 电流档** | 头文件注释 |
| 测量线制 | 2 线 / 4 线（开尔文） | `SetMeasLineMode` |
| 差分测量 | 通道 0&1 或 2&3 的差值，档位 10 V / 1 V | `SetDiffMode` |
| 带宽 | 高 / 低 | `ENUM_SMU_BW_MODE` |
| 钳位 | 电流钳位、电压钳位 | `CurrClamp` / `VoltClamp` |
| 告警 | `E_SMU_ALARM_CLAMP`、`E_SMU_ALARM_OPEN_SENSE` | `Alarm` |
| 测量 | `MeasureI` / `MeasureV` / `MeasureDiffV`，采样 **1~512 点**，间隔 **10~10000 µs** | 头文件 |
| 模式 | FVMV / FVMI / FIMV / FIMI / FNMV | 与 PMU 同 |

### 2.2 AWG

| 项 | 规格 | 出处 |
|---|---|---|
| 波形类型 | **SIN / RAMP / TRIANGULAR / PULSE** | `GenerateTypWave` |
| 点数 | **10 ~ 2,000,000** | 同上 |
| 偏移量 | 0 ~ 4 V | 同上 |
| 幅度 | 0 ~ 4 V | 同上 |
| 相位 | 0 ~ 360° | 同上 |
| 峰峰值 | 0 ~ 8 V（`SetAmpl`） | `SetAmpl` 注释 |
| 采样率 | **1 ~ 2 MHz** | `SetMode` |
| 发送模式 | `0x0` 连续 / `0x1` 非连续（按周期数）/ `0x02` DIO 触发 | 同上 |
| DDS | 非 DDS / DDS，倍频最大到 2 MHz | 同上 |
| 占空比 | **`dbPulseWidth / ulSample`** | 头文件注释 |

> ⚠️ **AWG 的 amplitude 与 offset 上限都是 4 V**，而 `SetAmpl` 的峰峰值可到 8 V。
> 若 DUT 需要 5 V 电平，AWG 单靠自身给不出（除非用 offset + amplitude 叠加，但两者各自 ≤4 V，合计可到 8 V —— 实际取决于波形定义，必须实测确认）。

---

## 三、测试项设计

### 3.1 SMU 类：三个层次

| # | 测试项 | 手法 | 目的 |
|:--:|---|---|---|
| S1 | **IDD 单点** | SMU `FVMV` 供 5 V，`MeasureI` 读静态电流 | 最基本的功能性功耗检查 |
| S2 | **供电电压扫描** | 同上，改 2.0 / 3.3 / 5.0 / 5.5 V 四点 | 验证宽压范围；抓"某电压点电流突增" |
| S3 | **VCC 脚 I-V 曲线** | 0→6 V 逐伏扫描，记录电流 | 抓闩锁、电源内短；看拐点 |

**为什么这三项要都做？** 它们抓的是不同失效：

- S1 只能判"通/不通"，一个 20 µA 的轻微漏电它可能放过；
- S2 能发现电压相关性（某些工艺缺陷只在特定偏压下显现）；
- S3 给的是**曲线**而不是点 —— 拐点位置直接指向失效机理（如 3.8 V 后电流陡增 = 弱闩锁）。

### 3.2 AWG 类：两个层次

| # | 测试项 | 手法 | 目的 |
|:--:|---|---|---|
| A1 | **直流电平（level sweep）** | PULSE 波形、占空比 99.9% → 近似直流，扫 0.5 V / 4.0 V | 用 AWG 替代 PinLevel 提供电平，验证 DUT 阈值响应 |
| A2 | **交流激励** | SIN / RAMP 波形 → DGT 或 DIO DSIO 捕获 | 测 ADC 的 DNL/INL、SNR；测运放的 CMRR/PSRR |

A1 是本工程实现的部分；A2 需要额外的采集配置，本方案给出**官方已验证的调用链**作参考（见 3.4）。

### 3.3 A1 的具体设计（本工程 `AWG_EN_ON_T` / `AWG_EN_OFF_T`）

**思路**：把 E3（使能脚）从 DIO 手里"接过来"，交给 AWG 驱动。

| 测试项 | AWG 输出 | 74HC138 期望响应 | 验证了什么 |
|---|---|---|---|
| `AWG_EN_ON_T` | 4.0 V（> VIH） | Y0 = L，其余 H | 高电平使能有效 |
| `AWG_EN_OFF_T` | 0.5 V（< VIL） | 全部 H | 低电平禁能有效 |

**为什么把 E3 交给 AWG 而不是留在 DIO？**

因为本工程的重点是演示 AWG。代价是 pattern 里**不能有 E3 这一列**（否则 DIO 会和 AWG 对打），所以：

- `.grp` 的 `CTRL_PINS` 只有 A0/A1/A2/E1/E2 **五个**信号；
- `pattern/*.csv` 的列 = 5 个输入 + 8 个输出 = **13 列**。

⚠️ **这个约束会连带影响 SMU 测试项**：SMU 测 IDD 时，所有输入脚必须有确定电平，否则悬空 CMOS 输入会虚增电流。所以三个 SMU 测试项里也**先让 AWG 给 4.0 V 使能**，再跑 pattern 建立地址/使能状态。

### 3.4 A2 的参考调用链（官方 ADC0832 案例，已上机验证）

**斜坡（测 ADC 静态 DNL/INL）**：

```cpp
awg.Signal("AWG_CH").Connect();
awg.Signal("AWG_CH")
   .GenerateTypWave("RAMP", "ADC_RAMP", 1638400, 0, 4, 0, 0)
   .LoadWaveFile("ADC_RAMP")
   .SetMode(2, 0, 1, 1638400, 2, 0, 0)   // 送模式=2(DIO触发), 非DDS, 1周期, 1638400点, 2MHz
   .SetAmpl(5);
```

**正弦（测 ADC 动态 SNR/SINAD/THD/ENOB）**：

```cpp
awg.Signal("AWG_CH").Connect();
awg.Signal("AWG_CH").GenerateTypWave("SIN", "ADC_SIN", 102400, 2, 2, 0);
awg.LoadWaveFile("ADC_SIN");
awg.SetMode(0x0, 0x0, 1, 102400, 1, 0, 0);   // 连续, 非DDS, 1周期, 102400点, 1MHz
awg.SetAmpl(5);
awg.Send();
```

**采集侧**（DIO 的 DSIO 捕获，非 DGT）：

```cpp
dio.Pattern("ADC_CH1_AWG").SetHRamMode(EN_CAP_MODE);
dio.Pattern("ADC_CH1_AWG").DSIO().SetCaptureMode(DSIOModeSerial)
                                 .SetAlignMode(MSB_MODE)
                                 .SetSampleSignal("DO")
                                 .SetSampleCycleCount(1)
                                 .SetSampleBits(8)
                                 .SetSampleEdge(2048);
dio.Pattern("ADC_CH1_AWG").SetTimeOut(20000).Run().WaitEnd();
dio.Pattern("ADC_CH1_AWG").DSIO().GetData(ADCOUT);
awg.Stop();
```

> 注意两个细节：**斜坡用 `SetMode(2, ...)` 走 DIO 触发**（保证采样与波形同步）；**正弦用 `SetMode(0x0, ...)` 连续输出**。这是官方工程的做法，不是随意选的。

---

---

## 四、测试条件与执行顺序

### 4.1 顺序不能乱（这是最容易出错的地方）

```
① AWG 给出 E3 电平
        ↓
② DIO/PinLevel 设置 VIH/VIL/VOH/VOL
        ↓
③ 跑 pattern 建立所有输入脚状态
        ↓
④ SMU 给 VCC 上电
        ↓
⑤ 延时 8 ms
        ↓
⑥ 测量
```

**为什么必须是这个顺序？**

| 若顺序错 | 后果 |
|---|---|
| 先上电、后建输入状态 | CMOS 输入脚悬空，会因输入级两管同时导通而虚增 IDD，**几倍到几十倍** |
| 先上电、后设 PinLevel | 输出脚被 DUT 驱动与 PinLevel 对抗，可能灌大电流 |
| 先测量、后延时 | 读到 SMU 环路建立过程中的瞬态，IDD 偏高 |

### 4.2 参数表

| # | 参数 | 取值 | 来源 / 理由 |
|:--:|---|---|---|
| 1 | SMU 电压档 | **±10 V**（`EN_SMU_VOLT_NP10V_RANGE`） | 5 V 供电留余量；不用 ±20 V 档（20 V 只能配 ≤100 mA） |
| 2 | SMU 电流档 | **10 mA** | 74HC138 静态电流 µA 级，10 mA 档兼顾分辨率与量程 |
| 3 | SMU 电流钳位 | **±50 mA** | 保护：万一 DUT 短路，限制在 50 mA |
| 4 | SMU 测量线制 | **4 线（开尔文）** | 见 4.3 |
| 5 | SMU 建立延时 | **8000 µs** | 官方 ADC0832 案例如是；SMU 环路比 PPMU 慢 |
| 6 | 测量采样 | `MEAS_AVG`，1 点，间隔 10 µs | 硬件侧已做平均 |
| 7 | AWG 波形 | PULSE，1024 点，pulseWidth = **1023** | 占空比 1023/1024 ≈ 99.9%，近似直流 |
| 8 | AWG 采样率 | 1 MHz | 直流用途，采样率不影响电平 |
| 9 | AWG 送模式 | `0x0` 连续 | 非连续模式会按周期数停，直流用途不适用 |
| 10 | AWG 幅度 | 4.0 V（使能）/ 0.5 V（禁能） | 见 4.4 |
| 11 | DIO VIH / VIL | 5.0 V / 0.0 V | 驱动 DUT 输入 |
| 12 | DIO VOH / VOL | 3.5 V / 1.5 V | 比较阈值，取 0.7×VCC 与 0.3×VCC |
| 13 | DIO 电压钳位 | 6 V / −1 V | 同官方样例 |

### 4.3 为什么用 4 线（开尔文）而不是 2 线

课程 PPT《电源模块功能介绍》专门讲了这一点：

| | 2 线 | 4 线 |
|---|---|---|
| 接线 | Force 与 Sense 合一 | **Force 与 Sense 分开** |
| 线阻与接触电阻 | **串进测量回路，直接叠加到读数** | Sense 几乎不吃电流，不产生压降 |
| 适用 | 大电流、对精度要求低 | **小电流、精密测量** |

举例：假设线阻 + 接触电阻共 0.5 Ω，测 20 µA 时压降只有 10 µV（可忽略）；但如果是给 DUT 供 50 mA，压降就是 **25 mV** —— 足以把一个 5.0 V 供电变成 4.975 V。

**IDD 测量真正关心的是 DUT 端电压准不准**，所以本工程用 4 线。接线时注意：Force 与 Sense 必须分别引到 DUT 引脚（Sense 就近接），不能在同一根线上分叉。

### 4.4 为什么使能电平取 4.0 V 而不是 5.0 V

这不是随便取的，是两条约束的交集：

| 约束 | 数值 |
|---|---|
| 74HC138 的 VIH 下限 | VIH ≈ 0.7 × VCC = **3.5 V**（VCC = 5 V 时） |
| AWG 的 amplitude 上限 | **4.0 V**（`GenerateTypWave` 文档：0~4 V） |

取 4.0 V 同时满足"高于 3.5 V 能可靠识别为高"和"不超过 AWG 能力"。**若强行要 5 V，AWG 给不出来。**

> 这是一个真实的工程约束：**用 AWG 当数字电平源时，它的输出摆幅往往比 DUT 的供电低**。此时可选的替代方案：用 offset 抬升（offset + amplitude 各自 ≤4 V），或干脆改用 PPMU/DPS 供电平、AWG 只做交流耦合。

---

## 五、限值

### 5.1 当前写入值（**全部是暂定值，必须标定**）

| 测试项 | 参数 | 下限 | 上限 | 单位 |
|---|---|---|---|---|
| IDD_SMU_T | IDD | 0 | 50 µA | A |
| IDD_SWEEP_SMU_T | IDD_2V0 / 3V3 / 5V0 / 5V5 | 0 | 50 µA | A |
| VCC_IV_SMU_T | I_VCC_0V ~ I_VCC_6V（7 点） | 0 | 100 µA | A |
| AWG_EN_ON_T | AWG_EN_ON | — | — | 由 pattern 比较 |
| AWG_EN_OFF_T | AWG_EN_OFF | — | — | 由 pattern 比较 |

⚠️ **50 µA / 100 µA 这两个数不是从 datasheet 抄的，是暂定的工程值**。必须按 5.2 标定。

### 5.2 AWG 类项的"限值"在哪

注意：`AWG_EN_ON_T` / `AWG_EN_OFF_T` 的判据**不在 `.tmf` 的 Min/Max 里**，而在两个地方：

| 判据 | 位置 | 含义 |
|---|---|---|
| 输出高低电平的**判定阈值** | `.cpp` 的 `pinlevel ... SetVoh(3.5).SetVol(1.5)` | DUT 输出 >3.5 V 判 H，<1.5 V 判 L |
| 每个向量的**期望值** | `pattern/*.csv` 的 `L` / `H` 列 | 该时刻输出应该是高还是低 |

`.tmf` 里那条 `AWG_EN_ON` 参数只是个占位（Min=Max=0），真正的判定走 `binObj.CheckResultAndBin(0, dioRes)` 的 `ST_DIO_RESULT` 重载。

### 5.3 标定方法（DC 参数通用）

1. 放宽窗口（IDD 设 0 ~ 1 mA），跑 **30~50 颗**已知良品；
2. 每颗记录 IDD，统计 μ、σ，收紧到 **μ ± 6σ**；
3. 叠加 GR&R 余量；
4. 用故意劣化的样品（如漏电偏大的片子）验证分离度；
5. **在最高工作温度下重跑一遍**（见 5.4）。

### 5.4 ⚠️ IDD 与温度是强相关，不是线性的

CMOS 静态电流的主要成分是**亚阈值漏电 + 结反偏漏电**，随温度**指数上升**（大致每升 10 °C 翻一倍）。

结论：
- 25 °C 标定的 50 µA 窗口，到 85 °C 可能不够 —— 良品会"自然"长到窗口外；
- **若要覆盖高温测试，必须在高温箱里重新标定**，不能拿常温限值直接用；
- 这一点和 OS 的 Vf 温度漂移（−2 mV/°C，线性）性质完全不同，不要混用经验。

---

## 六、工程实现

### 6.1 文件清单

```
templates/SMU_AWG_Demo/
├── SMU_AWG_Demo.job     工程入口
├── SMU_AWG_Demo.res     资源（沿用 4 组板卡定义）
├── SMU_AWG_Demo.sig     ★ SmuVdd(SMU) / E3_awg(AWG) / 13 个 DIO
├── SMU_AWG_Demo.grp     ★ CTRL_PINS(5) / OUTPUT_PINS(8) / DIO_ALL(13)
├── SMU_AWG_Demo.tim     WFT1，CTRL_PINS 驱动 / OUTPUT_PINS 9µs 比较
├── SMU_AWG_Demo.pat     ★ 2 个 pattern：AWG_EN_ON / AWG_EN_OFF
├── SMU_AWG_Demo.tmf     ★ 6 个测试项
├── pattern/awg_en_on.csv    ★ 期望 Y0=L，其余 H
├── pattern/awg_en_off.csv   ★ 期望全 H
├── Test.cpp             ★ SMU 3 项 + AWG 2 项 + SetPassBin
├── Makefile / interface.h
└── tools/
    ├── gen_tmf.py       生成并自校验 .tmf
    └── check_project.py 跨文件一致性校验（15 项）
```

### 6.2 通道映射

| 信号 | 类型 | `Site0_CH` | 说明 |
|---|---|:--:|---|
| SmuVdd | **SMU** | 1:1:0 | 4 线接 DUT 的 VCC(16) 与 GND(8) Sense |
| E3_awg | **AWG** | 1:1:0 | 接 DUT 的 E3(6) |
| A0 | DIO | 1:1:0 | pin1 |
| A1 | DIO | 1:1:1 | pin2 |
| A2 | DIO | 1:1:2 | pin3 |
| E1 | DIO | 1:1:3 | pin4 |
| E2 | DIO | 1:1:4 | pin5 |
| Y0~Y7 | DIO | 1:1:5~12 | pin15/14/13/12/11/10/9/7 |

⚠️ **SMU 与 AWG 都用 `1:1:0` 不冲突** —— 它们是**不同资源类型**的通道号，物理上由 loadboard 的继电器选择接到哪个 DUT 引脚。

### 6.3 六个测试项与 bin

| FN | FuncName | 仪器 | 参数 | Sbin |
|:--:|---|---|:--:|:--:|
| 0 | `IDD_SMU_T` | SMU | 1 | 2 |
| 1 | `IDD_SWEEP_SMU_T` | SMU | 4 | 3 |
| 2 | `VCC_IV_SMU_T` | SMU | 7 | 4 |
| 3 | `AWG_EN_ON_T` | AWG + DIO | 1（占位） | 5 |
| 4 | `AWG_EN_OFF_T` | AWG + DIO | 1（占位） | 6 |
| 5 | `SetPassBin` | — | 1 | 1 |
| — | 程序异常 | — | — | 99 |

bin 视图：2/3/4 = SMU 类失效（功耗/宽压/I-V），5 = 使能无效，6 = 禁能无效，99 = 工程结构问题。

### 6.4 代码要点

**SMU 上电（4 线）**：

```cpp
smu.Signal("SmuVdd").Connect();
smu.Signal("SmuVdd").SetMeasLineMode(EN_SMU_MEAS_LINE_4);   // ★ 4 线
smu.Signal("SmuVdd").SetMode("FVMV")
   .VoltForce(5.0, EN_SMU_VOLT_NP10V_RANGE)                 // ★ 必须带电压档枚举
   .CurrRange(10e-3)
   .CurrClamp(50e-3, -50e-3)                                // ★ 保护
   .Execute();
sys.DelayUs(8000);
```

**SMU 读电流**：

```cpp
vector<ST_MEAS_RESULT> r;
smu.Signal("SmuVdd").MeasureI(r, MEAS_AVG, 1, 10);
double idd = r.empty() ? 0.0 : r[0].dbValue;    // ★ 字段是 dbValue
```

**AWG 给恒定电平**：

```cpp
awg.Signal("E3_awg")
   .GenerateTypWave("PULSE", "AWG_DC_EN", 1024, 0, 4.0, 0, 0, 1, 1, 1023)
   //                            点数  偏移 幅度 相位 斜率 上升 下降 脉宽=1023/1024→99.9%
   .LoadWaveFile("AWG_DC_EN")
   .SetMode(0x0, 0x0, 1, 1024, 1, 0, 0)        // 连续 / 非DDS / 1周期 / 1024点 / 1MHz
   .SetAmpl(4.0);
awg.Signal("E3_awg").Send();
```

### 6.5 两个真实踩坑点

**① `Disconnect` 与 `DisConnect` 不是同一个方法**

```cpp
smu.Signal("SmuVdd").Disconnect();     // SMU：小写 c
dps.Signal("VCC").DisConnect();        // DPS：大写 C
ppmu.Signal("INOUT_PINS").DisConnect(); // PPMU：大写 C
```

三者拼写不同，写错会编译报错。这是看头文件才能发现的差异。

**② 复位必须把 SMU 归零再断开**

```cpp
smu.Signal("SmuVdd").SetMode("FVMV").VoltForce(0, ...).Execute();  // 先归零
smu.Signal("SmuVdd").Disconnect();                                  // 再断开
```

直接 `Disconnect()` 而不归零，某些配置下 DUT 会保留电荷；下一项测试若做 OS，读数会被残留电压污染。

---

---

## 七、验证与验收

### 7.1 步骤一：空跑（不接 DUT）

先 `make`，再让 Runner 依次调用 6 个测试项，**不接任何器件**。

| 检查点 | 期望 |
|---|---|
| 6 个测试项都被调用 | 日志里能看到 6 次进入 |
| 无 bin 99 | 没有程序异常 |
| 无硬件告警 | DPS/SMU 的 CLAMP、OPEN_SENSE 都不报 |

⚠️ 空跑时 SMU 会因开路而电流≈0、电压到位；各项大概率判 PASS（因为空载电流更小）。**空跑通过只证明工程结构对，不证明测量对。**

### 7.2 步骤二：万用表交叉验证 SMU

这是**必须做的一步**，因为本方案里 SMU 的代码没有任何官方样例可对照。

| 项 | 做法 | 通过标准 |
|---|---|---|
| 电压准确度 | SMU 设 5.0 V，用台式表测 DUT 侧 VCC-GND | 偏差 ≤ ±10 mV（4 线接法下） |
| 电流准确度 | 串一个已知电阻（如 10 kΩ）做负载，SMU 设 5 V | 读数 ≈ 5 V / 10 kΩ = 500 µA，偏差 ≤ 5% |
| 4 线是否生效 | 故意在 Force 线上串 1 Ω | 读数应几乎不变；若明显偏低说明退化成 2 线 |

### 7.3 步骤三：示波器验证 AWG 电平（**不可跳过**）

本方案用"99.9% 占空比的 PULSE"来近似直流，这是一个**假设**，必须实测确认：

| 检查点 | 通过标准 |
|---|---|
| 实际电平 | 设置 4.0 V 时，示波器测到 **4.0 V ± 0.2 V** 的平台 |
| 纹波 | 平台上的跌落/过冲 ≤ 0.3 V（占空比 99.9% 会有一次窄的低脉冲） |
| 0.5 V 档 | 同样测到 0.5 V ± 0.1 V |
| 建立时间 | `Send()` 后 5 ms 内稳定 |

⚠️ 若示波器测到的是"方波"而不是"平台"，说明占空比设置没生效（可能 `dbPulseWidth` 必须严格小于点数，或波形定义不同），**此时 A1 类测试项的整个思路要换**（改用 offset+amplitude 或直接上 PPMU）。

### 7.4 步骤四：Golden Sample 基线

| 检查点 | 通过标准 |
|---|---|
| IDD @5V | 应在 µA 量级，且与 datasheet 的 ICC 同数量级 |
| 四点扫描趋势 | IDD 随 VCC 缓升（不是陡增，也不是乱跳） |
| I-V 曲线 | 0~6 V 全程平缓，无明显拐点 |
| AWG_EN_ON | Y0 = L，其余 H，pattern 全通过 |
| AWG_EN_OFF | 全 H，pattern 全通过 |
| 重复性 | 同一颗连跑 10 次，IDD 极差 ≤ 读数的 20% |

### 7.5 步骤五：失效注入

| 样件 | 制造方法 | 期望 |
|---|---|---|
| F1 电源短路 | VCC 与 GND 间接 100 Ω | IDD 项 FAIL（5 V/100 Ω = 50 mA，撞钳位） |
| F2 输入悬空 | 拔掉 A0 线 | IDD 升高，可能触发限值 |
| F3 使能失效 | E3 到 DUT 的线断开 | AWG_EN_ON FAIL（输出不跟随） |
| F4 输出短路 | Y0 对地短接 | AWG_EN_ON FAIL |

### 7.6 验收判定

| # | 条件 | 权重 |
|:--:|---|---|
| 1 | 空跑 6 项正常，无 bin 99 | 必须 |
| 2 | SMU 电压/电流与台式表偏差在 ±10 mV / 5% 内 | 必须 |
| 3 | **示波器确认 AWG 电平是平台而非方波** | 必须 |
| 4 | Golden Sample 全部 PASS，I-V 曲线无拐点 | 必须 |
| 5 | 4 个失效样件全部被拦下 | 必须 |
| 6 | 限值经 30 颗以上标定 | 上产前必须 |

---

## 八、风险与本方案的边界

### 8.1 必须知道的三条边界（诚实说明）

| # | 边界 | 影响 | 应对 |
|:--:|---|---|---|
| 1 | **SMU 的代码没有官方样例可对照**<br>课程五个案例工程里**没有一处**调用 `smu.`，本方案的 SMU 代码是照 `usrInterface.h` 的签名逐条写的 | 首次 `make`/上机可能有意外 | 必须做 7.2 的万用表交叉验证 |
| 2 | **AWG 直流靠 99.9% 占空比近似** | 若波形定义与预期不符，电平会变成方波 | 必须做 7.3 的示波器验证；失败则改用 offset 抬升或换 PPMU |
| 3 | **无法在本机编译验证** | 语法/API 用法已核对头文件，但未真正 `make` 过 | 上机第一次 `make` 要逐行看输出 |

第 3 条的背景：本机 `D:\MINI-ATE\mingw64` 的 `cc1plus.exe` 一执行就段错误（exit 139），连 `int main(){}` 都编不了，属环境损坏。

### 8.2 工程坑清单

| # | 现象 | 原因 | 处置 |
|:--:|---|---|---|
| 1 | IDD 比预期大几倍 | 输入脚悬空；或先上电后建状态 | 按 4.1 的顺序重排 |
| 2 | I-V 曲线第一个点电流异常大 | 0 V 时器件未完全上电，读数无意义 | 0 V 点的限值可放宽，或从 1 V 起扫 |
| 3 | 电压扫描时电流逐点漂移 | SMU 尚未稳定就测 | 加大建立延时（8 ms → 20 ms） |
| 4 | AWG 电平不是平台 | 占空比没生效 | 见 7.3 |
| 5 | AWG 设 5 V 却给不出 5 V | amplitude 上限 4 V | 降到 4 V 或改用 offset 叠加 |
| 6 | 编译报 `DisConnect` 未定义 | SMU 是小写 `Disconnect` | 按 6.5 改 |
| 7 | 高温下 IDD 超限 | CMOS 漏电随温度指数上升 | 高温重标定（5.4） |
| 8 | `pattern` 报列数不匹配 | csv 列 ≠ `CTRL_PINS + OUTPUT_PINS` | 跑 `tools/check_project.py` |

### 8.3 什么时候不该用本方案

| 场景 | 更好的选择 |
|---|---|
| 只是为了给 DUT 供 5 V | **DPS**，别用 SMU |
| 每脚小电流漏电 | **PPMU**，能并行，快得多 |
| 需要 >20 V | 本平台给不了，需外接电源 |
| 需要精密 nA 级电流 | PPMU/BPMU 的 µA 档更合适；SMU 的定位不是精密源表 |
| 需要任意波形且 >8 V 峰峰 | AWG 给不了；考虑外接信号源 + 放大器 |

---

## 附录 A：上机顺序

```bash
# 1) 复制到测试机工作区
cp -r templates/SMU_AWG_Demo  D:/MINI-ATE/workspace/SMU_AWG_Demo

# 2) 改 .job 里的 jobDir / jobFilePath 为实际路径
# 3) 编译
cd D:/MINI-ATE/workspace/SMU_AWG_Demo
make

# 4) 不接 DUT 空跑（7.1）
# 5) 万用表交叉验证 SMU（7.2）—— 本方案必做
# 6) 示波器验证 AWG 电平（7.3）—— 本方案必做
# 7) 接 Golden Sample 跑基线（7.4）
# 8) 失效注入四个样件（7.5）
# 9) 30 颗以上标定限值（5.3），回写 .tmf
```

## 附录 B：与官方案例的对照

| 内容 | 官方有样例？ | 本方案依据 |
|---|:--:|---|
| DPS 供电 + IDD | ✅ CD4511 / TPS73625 | 直接照抄顺序与参数 |
| AWG 斜坡 / 正弦 | ✅ ADC0832 | 3.4 节的调用链取自官方原文 |
| DIO pattern + 比较 | ✅ CD4511 FUNC_T | `Test()` + `GetFailResult` + `CheckResultAndBin` 重载 |
| **SMU 任何用法** | ❌ **无** | 仅按 `usrInterface.h` 签名编写，**未经样例验证** |
| AWG 输出直流电平 | ❌ 无 | 本方案自创（99.9% 占空比），需示波器确认 |

## 附录 C：待确认事项

| # | 待确认 | 影响 |
|:--:|---|---|
| 1 | 测试机是否实际装了 SMU 板卡 | 没装则 SMU 三项无法运行；`Cali_SigDef.csv` / `Bist_SigDef.csv` 里**没有 SMU 条目**，需确认 |
| 2 | SMU 通道与 DUT 的物理接线（哪几对 Force/Sense） | 4 线接法必须实测通断 |
| 3 | AWG 的 99.9% 占空比实际输出形态 | 决定 A1 类测试项是否成立 |
| 4 | 是否需要高温测试 | 需要则 5.3 必须在高温箱重标 |
| 5 | 74HC138 的 IDD 实际数量级 | 决定 50 µA 暂定值是松还是紧 |

---

*本方案的 SMU 部分依据 `D:\MINI-ATE\runner\inc\usrInterface.h` 与 `usrDataType.h` 的签名编写，AWG 部分参照官方 ADC0832 案例；限值为暂定值，必须按第 5 节标定后使用。*



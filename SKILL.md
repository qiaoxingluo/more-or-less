---
name: st2500-ate-test-skill
description: 加速科技 ST2500/ST2516 ATE 平台的集成电路测试工程 skill。当用户讨论 ST2500、ST2516、加速科技测试机、ATE 测试工程、开短路 OS 测试、PPMU/BPMU/DPS 资源分配、Signal Map / Signal Group / Timing / TMF / Pattern 文件写法、引脚映射 SE-GIB100、或具体器件（CD4511、ADC0832、HK24C02、HK32F031F4P6、LM358P、TPS73625、74HC138）的测试流程时使用。核心内容包括 SMU 术语差异、PMU 五种模式、并行 OS 静态测量抓不到相邻脚短接的原理级盲区及双轮奇偶法修复方案、限值标定四步法、以及可直接编译的 HC138 工程模板。
agent_created: true
---

# ST2500/ST2516 ATE 集成电路测试工程 Skill

> 平台：加速科技 ST2500 / ST2516 + SE-GIB100 通用器件接口板
> 知识来源：《【课程资源】集成电路测试》课件与工程样例整理，2026-10-28

---

## 1. Skill 使命

本 Skill 用于在 **ST2500/ST2516 ATE 平台上从零开发一个 DUT（被测器件）的完整测试工程**。它提供：

- **确定的知识库**（术语、硬件资源、引脚映射、Limit 口径），避免凭空猜测；
- **可复用的方法**（OS 方案选型、限值标定、定位手法）；
- **可直接编译的工程模板**（`templates/HC138_OS_V1/`）。

**边界声明**：
- 不伪造数据。任何限值、频段、电流值都必须标注来源（手册 / 工程文件 / 业界惯例）。
- 涉及硬件连接有风险的操作（带电、未经 Golden Sample 验证的 Force 值），必须先提示风险。
- 当用户给出的器件型号查不到时，**如实说明并给出最接近的推断**，不要编造引脚定义。

---

## 2. 术语对齐（必读 —— ST2500 有一处术语陷阱）

ST2500 文档里的 **SMU 不是业界通用定义的 Source Measure Unit**。

| 缩写 | ST2500 语境 | 业界通用定义 | 说明 |
|---|:---|:---|:---|
| **SMU** | **高压电源板卡**（high-voltage power board） | Source Measure Unit（源测量单元） | 最容易翻车的一处。ST2500 的 SMU 是给 LCD 驱动 / PMIC 做高压测试的 UV 板，**不做常规源测量**。 |
| **PPMU** | Per-Pin PMU，**每个 DIO 通道各一个** | 同左 | OS / Leakage / DC 参数测试的主力，量程 ±40 mA。 |
| **BPMU** | Board PMU，每块板卡 3~4 通道 | —— | ±80 mA；电压范围 −2 ~ 9.25 V；电流档 5 µA / 20 µA / 200 µA / 2 mA / 80 mA。适合高精度或大电流场合。 |
| **DPS** | Device Power Supply | 同左 | 给 DUT 供电；OS 测试时反过来把 VCC **强置 0 V**。 |
| **DIO** | Digital I/O，数字通道 | 同左 | 注意「数字通道」= pin channel，不要理解成一根物理引脚。 |
| **RVS** | **Reference Voltage Source** | —— | 参考电压源。给数字通道提供 VIL / VIH / VOH / VOL 的**基准电压**。 |
| **CBIT** | Control Bit | —— | 控制位，用于继电器 / 外围电路开关（如断开 I²C 上拉电阻）。 |
| **AWG / DGT** | Arbitrary Waveform Generator / Digitizer | 同左 | 任意波形发生器（模拟源）与波形数字化（模拟测）。 |
| **TMU** | Time Measurement Unit | 同左 | 时间测量单元。 |
| **Pin Electronics / PE** | 单个待测引脚的所有功能电路 | 同左 | 包含驱动、比较、动态负载、PPMU、继电器四部分。 |

> **易错提醒**：RVS 是 Reference Voltage Source（参考电压源），不是 Reverse 也不是 Reset。Pin Level 里的 VIL/VIH 是相对 RVS 设定的。

---

## 3. ST2500 板卡资源速查（对应 `.res` 文件）

一份典型 `.res` 定义四组板卡，每组包含：

| 缩写 | 全称 | 数量/组 | 关键约束 |
|---|---|:--:|---|
| DPS | Device Power Supply | 4 | 供电；OS 时可强置 0 V |
| PPMU | Per-Pin PMU | 32 | ±40 mA |
| BPMU | Board PMU | 4 | ±80 mA；−2 ~ 9.25 V；5 µA / 20 µA / 200 µA / 2 mA / 80 mA 五档 |
| AWG | Arbitrary Waveform Generator | 1 | 模拟源 |
| DGT | Digitizer | 1 | 模拟测 |
| DIO | Digital I/O | 32 | 数字通道 |
| CBIT | Control Bit | 8 | 继电器控制 |
| TMU | Time Measurement Unit | 1 | 时间测量 |
| RVS | Reference Voltage Source | 1 | 电平基准 |

---

## 4. PMU 五种 force/measure 模式

| 模式 | 含义 | 典型用途 |
|---|---|---|
| **FVMV** | Force Voltage, Measure Voltage | 把引脚置 0 V 泄放残余电荷；配合继电器动作 |
| **FVMI** | Force Voltage, Measure Current | Leakage 测试（IIL / IIH）：给 VIH 或 VIL 电压，测流入/流出电流 |
| **FIMV** | Force Current, Measure Voltage | **OS 测试的标准用法**：force ±100 µA，测二极管 Vf |
| **FIMI** | Force Current, Measure Current | 较少使用，多用于校验电流通路本身 |
| **FNMV** | Force Nothing, Measure Voltage | 不驱动，仅观测引脚电压 |

**OS 为什么必须用 FIMV？**
PN 结正向压降 Vf 对**电流**呈对数关系（相对稳定），对**电压**呈指数关系（极其敏感）。force 电流才能得到可判别的 Vf 窗口；force 电压会得到指数级变化的电流，无法定限值。

---

## 5. ST2500 工程文件体系

```
<工程名>.job      ← 主入口：索引 res / sig / pat / tim / tmf / grp，指明 Test.dll
<工程名>.res      ← 资源定义：DPS/PPMU/BPMU/AWG/DGT/DIO/CBIT/TMU/RVS 数量
<工程名>.sig      ← Signal Map：信号名 → 物理通道（Site0_CH）
<工程名>.grp      ← Signal Group：把若干信号打包成组，便于并行操作
<工程名>.tim      ← Timing：WFT（Waveform Format Table）
<工程名>.pat      ← Pattern 索引
pattern/*.csv     ← Pattern 真值表（向量）
<工程名>.tmf      ← 测试流程：FuncList + Limits + Bin 映射
Test.cpp          ← C++ 测试代码（无 main，编译成 Test.dll）
Makefile          ← RUNNER_HOME = d:/MINI-ATE\runner
interface.h       ← #define USER_CODE extern "C" __declspec(dllexport)
```

### 5.1 `.tmf` 的结构（两个已踩过的坑）

⚠️ **坑一：FuncList 不在顶层**。真实路径是 `SiteList[0].FuncList[]`，用 `tmf.get("FuncList")` 会返回 `None`。

```
顶层字段：ChangeDate / Version / ProjectInitName / ProjectResetName / ProjectReuseName / SiteList
站点字段：SiteList[0].SiteNumber / SiteInitName / SiteResetName / LoopCount / ErrorSbin / ErrorHbin / BinLoopCount
流程项  ：SiteList[0].FuncList[i] → FuncNumber / FuncName / FuncDesc / LoopCount / Sbin / Hbin / Results / ResultAnalysis[]
限值项  ：ResultAnalysis[j] → ParamName / ParamNumber / Min / Max / Unit
```

⚠️ **坑二：`.tmf` 必须用 `utf-8-sig` 读取**（文件带 BOM）。用 `encoding='utf-8'` 会解析失败。

### 5.2 `.job` 关键点

```json
{ "Name":"HC138_OS_V1",
  "ResDef":"...res", "SigMap":"...sig", "Pattern":"...pat",
  "Timing":"...tim", "TMF":"...tmf", "SigGroup":"...grp",
  "Library":"Test.dll", "Makefile":"Makefile",
  "AteHomePath":"d:\\MINI-ATE",
  "lotId":"HC138_LXX1" }
```

### 5.3 标准开发流程（五步，不可乱序）

1. 编辑 **Signal Map**（`.sig`）
2. 编辑 **Signal Group**（`.grp`）
3. 新建 **Timing** 文件（`.tim`）
4. 编辑 **TMF**（`.tmf`）
5. 设定 **Limit**（`.tmf` 的 Min / Max + Bin）

### 5.4 `Test.cpp` 骨架必须记住的三点

- **没有 `main()`** —— Runner（测试机执行引擎）按 `.tmf` 中 FuncList 的顺序，**按函数名调用导出的函数**；
- `USER_CODE` 展开为 `extern "C" __declspec(dllexport)`；
- 每个测试函数必须包 `TEST_BEGIN` … `TEST_ERROR`（内部接 `binObj.HandlerException(0)`）… `TEST_END` 三段。缺少异常处理时，一旦中途报错，被测器件会保持在带电状态，有硬件损伤风险。

---

## 6. `.sig` 与 `.grp`：信号到通道的映射

`.sig` 中 `Site0_CH` 格式为 `板号:组号:通道号`。以 HC138 为例：

```
SigName,AFEType,ChType,Site0_CH
"VCC",DPS,Supply,1:1:0
"A0",DIO,Inout,1:1:0
"A1",DIO,Inout,1:1:1
"A2",DIO,Inout,1:1:2
"E1",DIO,Inout,1:1:3
"E2",DIO,Inout,1:1:4
"E3",DIO,Inout,1:1:5
"Y0",DIO,Inout,1:1:6
...
"Y7",DIO,Inout,1:1:13
```

`.grp` 把信号打包：

```
GrpName,SigName
"INPUT_PINS",A0,A1,A2,E1,E2,E3
"OUTPUT_PINS",Y0,Y1,Y2,Y3,Y4,Y5,Y6,Y7
"INOUT_PINS",A0,A1,A2,E1,E2,E3,Y0,Y1,Y2,Y3,Y4,Y5,Y6,Y7
```

代码里对**组**下命令，等于并行下发到该组所有管脚 —— `ppmu.Signal("INOUT_PINS")` 一句就是 14 个脚同时动作。这也是并行 OS 之所以快的根本原因。

---

## 7. Timing 中的四种输入波形格式（TSw）

同一份 Pattern（向量），四个 TSw 用不同的**输入驱动波形格式**来解释它：

| 格式 | 全称 | 波形特征 | 典型用途 |
|---|---|---|---|
| **NRZ** | Non-Return-to-Zero | 数据在整个周期保持，不归零 | 普通数据总线、常规输入 |
| **RZ** | Return-to-Zero | 数据脉冲只在**周期前半段**，后半段强制返回 0 | 需要保证下一周期起点从 0 开始时 |
| **RO** | Return-to-One | 数据脉冲在**周期前半段后返回 1** | 与 RZ 相反的场景（较少使用） |
| **SBC** | Surround-By-Complement | 数据脉冲之后（约半个周期）**追加一段反相脉冲** | 地址线（如 SRAM 地址输入）。能保证输入最终回到稳定态，避开内部 MUX / 比较器的建立时间 |

**输出比较沿（Output Strobe）**：通常在周期内设定一个采样点，用来判断输出是 0 还是 1。常见做法是设单一 strobe；对时序要求严的场景会使用 **Window + Strobe 双比较沿** —— 先用 window 粗判，再用 strobe 精确定位。

---

## 8. 开短路测试（OS / Open-Short）—— 本 Skill 的核心产出

**OS 永远排第一个测试项**，理由只有三条，但每条都很硬：

| 目的 | 说明 |
|---|---|
| 保护测试机 | 一根对 GND 短路的引脚，若在后续 FUNC 里被数字通道驱动到高电平，会灌大电流打坏 pin electronics 或烧 DPS。OS 先把这类器件拦下来。 |
| 停止浪费时间 | 打线没接上的芯片，后面的 IDD / VOH / VOL / FUNC 全是废数据，跑得越晚亏得越多。 |
| 自身诊断 | 探针卡脏、测试座针脚歪、自焊线脱落 —— 工程上统统表现为「某几根脚开路」。**OS 是 TE 定位治具问题的第一抓手**，单次 CP/FT 良率暴跌先看 OS 分布。 |

### 8.1 物理前提：不是所有引脚都有那两个二极管

教科书方案的前提是「每个 IO 脚内部有一对**首尾相接的 ESD 保护二极管**，上接 VDD、下接 GND」。

⚠️ 这个前提**不是普遍成立的**，已知例外：

| 例外情况 | 后果 |
|---|---|
| RF / 高速串行引脚 | 二极管寄生电容会劣化高频性能，**常常刻意不接** |
| 5 V tolerant I/O | 靠 NMOS 堆叠承受高压，若保留传统上二极管会导致漏电或闩锁风险，**常常没有到 VDD 的上二极管** |
| 部分模拟专用脚、高压脚 | 内部结构不同，OSP 限值需要单独定，甚至不能用静态法 |
| **74LS138（TTL 双极型）** | TTL 输入是发射极结构，**不存在标准的 VCC 侧二极管** |

由此导出两条工程规矩：

1. **OS 条件必须按引脚分组定制**，不是所有脚共用一套。缺失上二极管的脚只跑 OSN。
2. **任何芯片开跑前，先拿一颗已知良品（Golden Sample）确认所有脚读数都在窗口内**。这一步省了，后面全是假失效。

### 8.2 ★ 核心发现：并行静态法抓不到相邻脚短接

除 GND 外所有脚同时加电流、同时测量 —— 这就是 `CD4511_v1` 工程采用的做法（并行 PPMU FIMV）。它有一个**原理级盲区**：

```
两根相邻脚（假设引脚 1 与 2）短接时：

  每台 PPMU 仍在各自抽 100 µA
      → 短接节点总共要流走 200 µA

  该节点上有两只并联的下二极管
      → 200 µA 平均分流，每只二极管还是 100 µA

  二极管正向压降 Vf 是电流的【对数】函数
      → 电流没变，Vf 就基本不变

  结果：两只脚都读到正常的 −0.65 V → 双双 PASS ❌
```

⚠️ **调紧限值也救不了**，因为读数根本没有偏移。这是一个原理级盲区，不是参数级问题。

### 8.3 修复方案：双轮奇偶法（Odd/Even Two-Pass）

关键在于它把「分流」变成了「硬短路」：

- **第一轮**：奇数脚 force ±100 µA，**偶数脚由 PMU 硬钳到 0 V**；
- **第二轮**：反过来，偶数脚 force，奇数脚钳 0 V。

于是引脚 1-2 短接时：

- 第一轮里 2 号脚被强置 0 V；
- 加到 1 号脚上的 100 µA 直接经短接点灌进地 → **读数被拉到 ≈ 0 V**；
- 0 V 远低于下限 −0.2 V → **硬 FAIL**，同时暴露出两根脚的序号。

⚠️ **分组必须按物理相邻关系**，不能按信号名顺序：

- DIP / SOP / TSSOP：管脚编号即物理顺序，奇偶分组天然保证相邻脚异号 ✓
- **BGA / QFN：必须按物理坐标分组**，否则同一个 pad 下面的两个球可能同号。

### 8.4 五种方案的成本 vs 覆盖对照表（以 14 个信号脚为例）

| 方案 | 测量次数 | 抓开路 | 抓对 GND/VCC 短接 | 抓相邻脚短接 | 适用 |
|---|:--:|:--:|:--:|:--:|---|
| ① 串行静态（Serial PMU FIMV） | 28 | ✓ | ✓ | ✓ | 调试 / 失效分析 |
| ② **双轮双方向** | 4 | ✓ | ✓ | ✓ | **脚数少时推荐，覆盖最全** |
| ③ **双轮单方向（OSN）** | 2 | ✓ | ✓ | ✓ | **性价比最高** |
| ④ 并行双方向（课件 CD4511 方案） | 2 | ✓ | ✓ | ✗ | 授课标准；量产时不够 |
| ⑤ 并行单方向 | 1 | ✓ | ✓ | ✗ | 极限量产 |
| ⑥ 数字 OS pattern + PMU 串行兜底 | 1 段 pattern | ✓ | ✓ | ✓ | 大批量量产的最优解 |

**一个反直觉但很好用的结论：**

> 课件用的「并行双方向」要 2 次测量；而「双轮单方向」也要 2 次测量，却能额外覆盖相邻脚短接。
> **同样的测试时间，换成双轮单方向是纯赚的。**

（代价是不再验证上二极管。但对 CMOS 器件来说，上二极管缺失很少是致命缺陷，通常交给 Leakage / ESD 抽检兜底。）

**方案 ⑥ 的前提必须在硬件阶段决定**：画 Load Board / DUT Board 时必须同时把**数字源和模拟源都分配到每个引脚**，这是事后改不了的事。

**方案补充（无二极管引脚）**：对 RF 脚、5 V tolerant IO 这类没有 ESD 二极管的引脚，静态二极管法失效，改用：

- 电容 / 电荷法：向引脚灌一个小电流脉冲，测电压上升斜率或最终电压 —— 开路时只对寄生电容 C 充电、电压迅速冲到钳位；
- 或直接测引脚对地电容是否在预期范围内；
- 或退一步：该脚只跑单方向（OSN），把保护结构的验证交给下游 Leakage / FUNC / ESD 抽检。

### 8.5 OS 的六条工程坑

| # | 坑 | 说明 |
|---|---|---|
| 1 | **VCC 必须强置 0 V** | 上二极管的回路要靠 VCC 这条低阻通路构成。VCC 悬空时，全好的芯片 OSP 也会读到钳位电压，表现为「整批开路」。这就是 `CD4511_v1` 里 `dps.Signal("VDD").SetMode("FVMV").VoltForce(0)` 那行的全部意义。 |
| 2 | **GND 不占 ATE 通道** | 硬线接接口板系统地。SE-GIB100 上用 **b7 针脚的 GND**，注意别接到 AGND（b2 / b5 / b12 / b17 / b23 / b28）—— 那是模拟地，不是同一个网络。 |
| 3 | **电源脚不能被当普通 IO 测** | 内部结构不同，标准 OS 限值不适用。但症状本身就是诊断信息：VCC 开路 → 所有上二极管测试集体失效；GND 开路 → 所有下二极管测试集体失效。 |
| 4 | **板上的上拉电阻会污染结果** | I²C 的 SDA / SCL 会上拉到 VCC。做 OS 时 VCC 被拉到 0 V，上拉电阻会把被测脚拽向地，读数偏离 Vf。HK24C02 案例明确要求用继电器物理断开。 |
| 5 | **温度漂移** | 二极管 Vf 约 **−2 mV/°C**。25 °C→85 °C，标称 0.65 V 跌到约 0.53 V，仍在 0.2~1.2 V 窗口内，余量够。但**如果把下限提到 0.5 V 以上，高温一定会误杀**。 |
| 6 | **必须留建立延时** | `Measure()` 前必须有 `sys.DelayUs(5000)`。去掉延时读到的是瞬态，数据会飘，`±6σ` 标定会直接失败。 |

---

## 9. Limits 标定方法 —— 限值不是查出来的，是标定出来的

这是最常见的作业 / 新人错误：**把 0.2 / 1.2 当成规格书数字抄过来**。

翻遍各家资料，窗口本身就不统一：

| 来源 | 下限 | 上限 |
|---|---|---|
| ST2500 手册 / `CD4511_v1.tmf` 工程 | 0.2 V | 1.2 V |
| 通用业界口径 | 0.2 V | 1.5 V |
| 期刊《基于 ATE 的 IC 测试原理、方法及故障分析》 | 0.1 V | 1.2 V |
| **课件 PPT `7.课件（CD4511）` 第 15 页** | 0.2 V | **0.8 V** ⚠️ |

⚠️ **注意最后一行**：同一门课，PPT 讲稿写 0.8 V，随 `CD4511_v1` 工程下发的 `.tmf` 里写的却是 1.2 V。**实际以工程文件为准** —— 0.8 V 上限会把 Vf 偏高但完全正常的管子误杀。

**标定四步：**

1. 先放宽窗口（如 0.05 ~ 1.8 V），跑 **30~50 颗已知良品**；
2. 统计每脚的均值 μ 与标准差 σ，收紧到 **μ ± 6σ**；
3. 叠加 **GR&R**（设备复现性）余量；
4. 再跑故意破坏的坏品（如挑掉一根线的 Sample），确认**良品簇与失效簇之间有清晰间隔**才定稿。

**另外两个必须记住的量：**

- **电压钳位必须设在窗口之外**（通常 ±2 V）。若钳位设成 ±1.2 V，开路引脚的读数会落在窗口边界上，判不出来。
- **电流别超过 ~100 µA**。OS 是真的让 ESD 二极管正偏导通，电流过大可能损伤器件 —— 这也是业界普遍取 100 µA 的原因。

---

## 10. 官方五款案例的测试项清单（实测 `.tmf`，非推测）

> `3.程序案例/2.半导体工程师实训平台—五款案例/`
> 另有 `1.通用器件接口板—一款案例/CD4511_v1/`（DIP-16 自焊线，是 HC138 的移植母版）。

**命名约定**：`FT` = Final Test（封装成品测试），`CP` = Circuit Probe（晶圆级探针测试）。同一个 `.tmf` 结构对两者通用，差别在 runtime 由 prober/handler 提供。

### 10.1 ADC0832（8 位 ADC，FT）

| FN | 测试项 | 参数数 | 参数 |
|:--:|---|:--:|---|
| 0 | **OS_TEST** | 13 | OSN_CS / OSN_CH0 / OSN_CH1 / OSN_DI / OSN_DO / OSN_CLK / **OSN_VCC_DIO** ／ OSP_CS / OSP_CH0 / OSP_CH1 / OSP_DI / OSP_DO / OSP_CLK |
| 1 | ADC_Static_AWG | 6 | Offset, Gain_Error, Miss_Code, **DNL, INL**, ADC_OUT_Lin |
| 2 | ADC_Dynamic_DSIO | 4 | **SNR, SINAD, THD, ENOB** |
| 3 | SetPassBin | 1 | — |

⚠️ **值得注意**：VCC 脚只有 `OSN_VCC_DIO`，**没有对应的 OSP**。这正是第 8.5 节第 3 条坑的实证 —— 电源脚内部结构不同，不能与普通 IO 共用同一套 OS 条件。

### 10.2 HK24C02（I²C EEPROM，CP）

| FN | 测试项 | 参数 |
|:--:|---|---|
| 0 | OS | os_icc, OS_SCL, OS_SDA |
| 1 | leakage | leak_sda_H / leak_scl_H / leak_sda_L / leak_scl_L |
| 2 | standby | 静态功耗电流 |
| 3 | WRITE_55AA_HV | TM_ALLEVEN_55_2K, TM_ALLODD_AA_2K |
| 4 | READ_55AA_HV | 回读校验 |
| 5 | SetPassBin | — |

⭐ **`TM_ALLEVEN_55_2K` / `TM_ALLODD_AA_2K` 就是存储器 MBIST 的经典手法**：向全偶数地址写 0x55、全奇数地址写 0xAA（棋盘/交替图样），覆盖地址译码故障与相邻单元耦合故障。

### 10.3 HK32F031F4P6（Cortex-M0 MCU，FT）—— 项数最多，14 项

OSN_TEST / OSP_TEST（各 **17 个 GPIO**）→ VOH_TEST → VOL_TEST → **VOPD_TEST**（内部下拉）→ **VOPU_TEST**（内部上拉）→ NFL_TEST → **HSE_TEST**（外部高速晶振）→ PVD_TEST（电源监视）→ **BIST_TEST** → **HSI_TEST**（内部高速 RC）→ **PLL_TEST** → **LSI_TEST**（内部低速 RC）→ SetPassBin。

MCU 类的特色：必须验证内部时钟源（HSI/LSI/PLL）、内部上下拉电阻、电源监视阈值。这些参数无法从外部引脚直接测，靠芯片自身 BIST 或特定寄存器配置后跑 pattern 实现。

### 10.4 LM358P（双运放，FT）

| FN | 测试项 | 参数 |
|:--:|---|---|
| 0 | **Ios_Uos_TEST** | Uos（输入失调电压）, Ios（输入失调电流） |
| 1 | **CMRR** | 共模抑制比 |
| 2 | **Aus** | 开环电压增益 |
| 3 | **PSRR** | 电源抑制比 |
| 4 | **icc** | 静态功耗电流 |
| 5 | SetPassBin | — |

💡 **这五个参数就是「两毛钱运放 vs 二十块钱运放」的全部差距所在**。廉价运放的 Uos 可达 mV 级、Ios 达几十 nA、CMRR 只有 60~70 dB、开环增益低、PSRR 差、温漂大；精密运放（OPA 系列）靠激光修调把 Uos 做到 µV 级。**用万用表和示波器分不出来，只有 ATE 的这四个 DC 测试项能定量拉开差距。**

### 10.5 TPS73625（LDO 线性稳压器，FT）—— 我最推荐的教学案例

OS_TEST（IN/OUT/EN）→ VOUT_TEST（6 组条件：**3 种输入电压 × 2 种负载** 2.7V/4V/5.5V × 10mA/400mA）→ LNR_TEST（线性调整率）→ VDO_TEST（压差）→ LDR_TEST（负载调整率）→ ICL_TEST（限流点）→ IGND_TEST（静态电流/地电流）→ PSRR_TEST（100 Hz 与 10 kHz 两点）。

💡 **推荐理由**：电源类器件的每个测试项都对应一个 datasheet 上的明确规格和一个直观的物理意义，**不需要写 Pattern**，只需 DPS + PMU 组合即可 —— 对初学者最友好，同时对 Test Condition 的理解要求最高。

---

## 11. ATE 通用测试项速查

| 测试项 | 缩写 | 做法要点 | 典型失败模式 |
|---|---|---|---|
| 开短路 | OS / Open-Short | PPMU FIMV ±100 µA，测 ESD 二极管 Vf | 开路打到钳位 ±2 V；短接到地读到 ≈0 V |
| 漏电流 | IIH / IIL | FVMI：给 VIH 或 VIL 电压，测流入/流出电流 | 栅氧击穿、ESD 损伤 → 漏电超 µA 级 |
| 静态功耗 | IDD Static | DPS 供电，pattern 停在稳态，测总电流 | CMOS 闩锁、栅漏电 |
| 动态功耗 | IDD Dynamic | 跑一段翻转 pattern 时测平均电流 | 内部短路、时钟树异常 |
| 输出高电平 | VOH / IOH | 拉规定负载电流 IOH，测输出≥VOH | 上拉管导通电阻偏大 |
| 输出低电平 | VOL / IOL | 灌规定负载电流 IOL，测输出≤VOL | 下拉管能力不足 |
| 输入阈值 | VIL / VIH | 扫输入电压找输出翻转点 | 工艺偏移导致噪声容限不足 |
| 功能测试 | Functional / Pattern | 送真值表向量，比较输出, e.g. 用 OEM pattern | 逻辑错误、时序违例、存储单元失效 |
| ADC 静态 | Offset / Gain / DNL / INL | AWG 送斜坡输入，逐码采样统计码密度 | 失码（Miss Code）、非线性 |
| ADC 动态 | SNR / SINAD / THD / ENOB | AWG 送正弦，DGT 采样后 FFT | 噪声、谐波失真 |

---

## 12. SE-GIB100 通用器件接口板 —— 物理引脚映射（实测）

**核心公式：插槽针号 = 32 − CHn**

| CHn | 插槽针号 | 备注 |
|:--:|:--:|---|
| CH0 | 32 | `_HV` 版本，可用 |
| CH1 | 31 | |
| CH2 | 30 | |
| … | … | |
| CH7 | 25 | |
| CH8 | 24 | `_HV` 版本，可用 |
| CH13 | 19 | 14 通道方案的最后一个信号脚 |
| CH16 | 16 | `_HV` 版本，可用 |
| CH24 | 8 | `_HV` 版本，可用 |

- **DPS**：Force / Sense CH0 → **c32 / b32**
- **GND（系统地）→ b7** —— DUT 的地就焊这里，**不占 ATE 通道**
- ⚠️ **AGND（模拟地）在 b2 / b5 / b12 / b17 / b23 / b28 —— 与 GND 不是同一个网络，绝对不能混接**

---

## 13. 74HC138（HC138）速查 —— 配套工程模板的主角

**3-to-8 线译码器 / 解复用器，DIP-16，标准 CMOS，14 个信号脚均有完整上下二极管。**

| 引脚 | 名称 | 说明 | 引脚 | 名称 | 说明 |
|:--:|---|:--|:--:|---|:--|
| 1 | A0 | 地址输入（低位） | 16 | **VCC** | 电源 2~6 V |
| 2 | A1 | 地址输入 | 8 | **GND** | 地 → 插槽 b7 |
| 3 | A2 | 地址输入（高位） | 15 | Y0 | 输出（低有效） |
| 4 | **E1** | 使能（**低有效**） | 14 | Y1 | 输出（低有效） |
| 5 | **E2** | 使能（**低有效**） | 13 | Y2 | 输出（低有效） |
| 6 | **E3** | 使能（**高有效**） | 12 | Y3 | 输出（低有效） |
| | | | 11 | Y4 | 输出（低有效） |
| | | | 10 | Y5 | 输出（低有效） |
| | | | 9 | Y6 | 输出（低有效） |
| | | | 7 | Y7 | 输出（低有效） |

**使能条件**：`E1=0 且 E2=0 且 E3=1`，否则**所有输出恒为高**。这是功能测试（FUNC）的核心真值表。

⚠️ **型号澄清**：
- 查遍 datasheet 库都不存在独立的 "HCE138" 型号，**实际应为 74HC138**（或厂商前缀变体 `SN74HC138N` / `MC74HC138` / `TC74HC138`，引脚定义完全一致，流程通用）。
- 若为 **74LS138（TTL）而非 74HC138（CMOS）**：TTL 输入是发射极结构，**没有标准 VCC 侧二极管** → OSP 会全体读到钳位电压 → **必须改为只跑 OSN**，并相应重新标定限值。

### 13.1 配套工程模板：`templates/HC138_OS_V1/`

已验证可解析的完整工程骨架（`.job/.res/.sig/.grp/.tim/.pat/pattern/func.csv/.tmf/Test.cpp/Makefile/interface.h`），实现了课件标准的并行双向 OS。

使用方式：

```bash
cp -r templates/HC138_OS_V1  D:/MINI-ATE/workspace/<新工程名>
# 然后按五步法改造：改 .sig → 改 .grp → 改 .tim → 改 .tmf → 定 Limit
```

`Test.cpp` 中 `OSN_T()` / `OSP_T()` 的关键链（可直接复用）：

```cpp
dps.Signal("VCC").Connect();
ppmu.Signal("INOUT_PINS").Connect();
sys.DelayUs(5000);

// ★ VCC 必须强置 0V —— 给上二极管提供回路，否则整批误报开路
dps.Signal("VCC").SetMode("FVMV").VoltForce(0)
                 .CurrRange(0.5).CurrClamp(0.5, -0.5).Execute();
sys.DelayUs(5000);

// 先全部置 0V 泄放残余电荷，保证同起始态
ppmu.Signal("INOUT_PINS").SetMode("FVMV").VoltForce(0)
                         .CurrRange(40e-3).Execute();
sys.DelayUs(5000);

// ★ FIMV ±100µA，读数落在 ±(0.2~1.2)V；钳位 ±2V 必须在窗口之外
ppmu.Signal("INOUT_PINS").SetMode("FIMV").CurrForce(-100e-6)
                         .CurrRange(100e-6).VoltClamp(2, -2).Execute();
sys.DelayUs(5000);              // ★ 建立延时，缺了数据会飘

ppmu.Signal("INOUT_PINS").Measure(OSN_RESULT);
binObj.CheckResultAndBin(0, OSN_RESULT, 14);
```

函数尾部必须接复位块（VCC→0 V、INOUT_PINS→0 V、`DisConnect()`、`DelayUs(5000)`），否则下一个测试项会继承上一个的偏置状态。

**若要升级为双轮奇偶法**：在 `.grp` 里增补 `ODD_PINS` / `EVEN_PINS` 两组，`.tmf` 里把每个方向的 1 次测量扩成 2 次，并在 `Test.cpp` 中对「未被测的另一组」用 `SetMode("FVMV").VoltForce(0)` 硬钳。

---

## 14. 使用本 Skill 的操作流程

1. **先读对应 reference**，不要凭记忆作答（见下表）；
2. **参数必须定量**，标注来源。用户明确反对虚假数据与盲猜；
3. **给结论前先自查术语陷阱**（尤其 SMU，见第 2 节）；
4. **涉及 Limit 时**，先声明这是标定值不是 datasheet 值，并给出第 9 节的四步标定流程；
5. **新工程一律先复制模板** 再改 pinmap，不要从空文件手写；
6. **型号查不到时如实说明**并给出最接近的推断 + 需要用户确认的点。

| 用户想聊的话题 | 读哪个 reference |
|---|---|
| 术语 / PMU 模式 / 板卡资源 | `references/01-ST2500平台与术语.md` |
| ATE 通用测试项定义 | `SKILL.md` 第 11 节 + `references/02-ATE核心测试项.md` |
| OS 方案选型、为何抓不到相邻短接 | `references/03-开短路OS方法论.md` |
| 工程文件体系怎么改 | `references/04-工程文件体系.md` |
| 插槽针脚 / AGND / 接线 | `references/05-SE-GIB100硬件映射.md` |
| 74HC138 / "HCE138" 澄清 | `references/06-74HC138速查.md` |
| Limit 标定 / 单位换算 | `references/07-Limits标定与六条坑.md` |
| 某类器件的完整流程怎么排 | `references/08-五款案例测试流程.md` |

---

## 15. 附录

### `scripts/tmf_dump.py`

解析任意 `.tmf` 并打印 FuncList / 参数名 / Limits 的命令行工具。它固化了两个坑的处理：`utf-8-sig` 编码与 `SiteList[0].FuncList[]` 路径。

```bash
python scripts/tmf_dump.py <目录或文件>        # 概览
python scripts/tmf_dump.py <目录或文件> -v     # 附带每项的 Min/Max/Unit
```

### `templates/HC138_OS_V1/`

可直接在 `D:/MINI-ATE/workspace/` 下 `make` 的完整工程骨架，见第 13.1 节。



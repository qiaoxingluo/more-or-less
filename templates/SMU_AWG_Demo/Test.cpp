//==============================================================================
// SMU_AWG_Demo —— 用 SMU 与 AWG 完成 DUT 测试的标准模板
//
// 载体器件：74HC138（DIP-16，3-8 译码器）—— 因为板子已焊好、引脚已知
//
// 本工程演示两件事：
//   【A】用 SMU 做 DC 参数测试
//        - IDD 静态功耗（单点）
//        - IDD 电压扫描（2.0 / 3.3 / 5.0 / 5.5 V 四点）
//        - VCC 脚 I-V 曲线（0~6 V 扫 7 点，查闩锁 / 电源内短）
//   【B】用 AWG 做模拟激励
//        - AWG 直接给 E3 使能脚提供 4.0 V（使能）→ 期望 Y0=L
//        - AWG 给 0.5 V（禁能）→ 期望输出全 H
//
// ⚠️ 重要前提（与 OS 工程不同）：
//   本工程把 E3 交给 AWG 驱动，pattern 里**不含 E3 列**。
//   因此 .grp 的 CTRL_PINS 只有 A0/A1/A2/E1/E2 五个信号。
//   E3 的电平在整个测试过程中由 AWG 提供，SMU 测试项里也保持 4.0V 使能，
//   这样才能保证所有输入脚都有确定电平（悬空 CMOS 输入会虚增 IDD）。
//
// ⚠️ 关于 SMU 的量程：ST2500 的 SMU 是【高压电源模块 / 独立 4 通道电压板卡】，
//   不是业界意义的 Source Measure Unit。它电压档分 ±1/±5/±10/±20V，
//   20V 档只能配 ≤100mA 电流档。
//==============================================================================
#include "interface.h"
#include <iostream>
#include <string>
#include <vector>

using namespace std;

unsigned long SITE = 0, SITE_ID = 0, ACTIVE_SITE_NUM = 0;

//------------------------------------------------------------------------------
// 参数集中管理
//------------------------------------------------------------------------------
static const double SMU_VCC_DEFAULT = 5.0;      // 常规供电
static const double SMU_IRANGE      = 10e-3;    // 10mA 电流档（够 HC138 用）
static const double SMU_ICLAMP      = 50e-3;    // ±50mA 保护
static const int    SMU_SETTLE_US   = 8000;     // SMU 建立延时

static const double AWG_EN_HIGH     = 4.0;      // 使能电平：74HC 的 VIH≈0.7×VCC(5V→3.5V)，4.0V 够；
                                                // 取 4.0 而非 5.0 是因为 AWG 的 amplitude 上限只有 4V
static const double AWG_EN_LOW      = 0.5;      // 禁能电平：74HC 的 VIL≈0.3×VCC(5V→1.5V)，0.5V 远低于

//------------------------------------------------------------------------------
// 工程级 / Site 级钩子
//------------------------------------------------------------------------------
USER_CODE void ProjectReuse() { cout << "ProjectReuse" << endl; }
USER_CODE void ProjectInit()  { cout << "ProjectInit" << endl; }
USER_CODE void ProjectReset() { cout << "ProjectReset" << endl; }
USER_CODE void site_init()    { cout << "site_init" << endl; }
USER_CODE void site_reset()   { cout << "site_reset" << endl; }

void EndOfWafer() { cout << "EndOfWafer" << endl; }
void EndOfLot()   { cout << "EndOfLot" << endl; }

inline void Enable_EOW_Func() { proberObj.EnableWaferEndCheck(EndOfWafer); }
inline void Enable_EOL_Func() { proberObj.EnableLotEndCheck(EndOfLot); }

//------------------------------------------------------------------------------
// 工具：构造一条测量结果
//------------------------------------------------------------------------------
static ST_MEAS_RESULT MakeResult(const string &name, double value)
{
	ST_MEAS_RESULT m;
	m.strSigName      = name;
	m.ulSite          = SITE;
	m.dbValue         = value;
	m.ulAlarmBehavior = 0;
	m.ulChipAlarmType = 0;
	return m;
}

//------------------------------------------------------------------------------
// AWG：输出恒定电平
//
// 手法：用 PULSE 波形把占空比拉到接近 100%（pulseWidth = 1023 / 1024），
//       得到近似直流。offset 给 0、amplitude 给目标电压。
//
// ⚠️ AWG 的 amplitude 上限是 4V（GenerateTypWave 文档：0~4V），
//    SetAmpl 的峰峰值上限是 8V。所以本模板能给的直流范围是 0 ~ 4V。
//    若要 5V 使能，请改用 DPS/PPMU 提供，或把 DUT 供电降到 4V 以内。
//------------------------------------------------------------------------------
static void AWG_SetDcLevel(const string &wavName, double volt)
{
	awg.Signal("E3_awg").Connect();
	sys.DelayUs(5000);

	awg.Signal("E3_awg")
	   .GenerateTypWave("PULSE", wavName, 1024, 0, volt, 0, 0, 1, 1, 1023)
	   .LoadWaveFile(wavName)
	   .SetMode(0x0, 0x0, 1, 1024, 1, 0, 0)   // 连续、非DDS、1周期、1024点、1MHz
	   .SetAmpl(volt);
	sys.DelayUs(5000);

	awg.Signal("E3_awg").Send();
	sys.DelayUs(5000);
}

//------------------------------------------------------------------------------
// DIO 引脚电平设置
//------------------------------------------------------------------------------
static void DIO_Setup()
{
	pinlevel.Signal("DIO_ALL").Connect();
	sys.DelayUs(5000);

	pinlevel.Signal("DIO_ALL").SetMode("DIO")
			.SetVih(5.0).SetVil(0.0)
			.SetVoh(3.5).SetVol(1.5)
			.SetIoh(0).SetIol(0).SetVt(0)
			.VoltClamp(6, -1)
			.Execute();
	sys.DelayUs(5000);
}

static void DIO_Off()
{
	pinlevel.Signal("DIO_ALL").SetMode("DIO")
			.SetVih(0).SetVil(0)
			.SetVoh(0).SetVol(0)
			.SetIoh(0).SetIol(0).SetVt(0)
			.VoltClamp(6, -1)
			.Execute();
	pinlevel.Signal("DIO_ALL").DisConnect();
	sys.DelayUs(5000);
}

//------------------------------------------------------------------------------
// SMU：4 线法供电 + 电流测量
//
// 为什么用 4 线（开尔文）：2 线时线阻与接触电阻会串进测量回路，
// 在 mA 级电流下压降可达几十 mV，IDD 这类小电流测量会被污染。
// 4 线把 force 与 sense 分开，sense 几乎不吃电流，读数即 DUT 端真实电压。
//------------------------------------------------------------------------------
static void SMU_PowerOn(double volt)
{
	smu.Signal("SmuVdd").Connect();
	sys.DelayUs(SMU_SETTLE_US);

	smu.Signal("SmuVdd").SetMeasLineMode(EN_SMU_MEAS_LINE_4);
	smu.Signal("SmuVdd").SetMode("FVMV")
	   .VoltForce(volt, EN_SMU_VOLT_NP10V_RANGE)
	   .CurrRange(SMU_IRANGE)
	   .CurrClamp(SMU_ICLAMP, -SMU_ICLAMP)
	   .Execute();
	sys.DelayUs(SMU_SETTLE_US);
}

static void SMU_PowerOff()
{
	smu.Signal("SmuVdd").SetMode("FVMV")
	   .VoltForce(0, EN_SMU_VOLT_NP10V_RANGE)
	   .CurrRange(SMU_IRANGE)
	   .CurrClamp(SMU_ICLAMP, -SMU_ICLAMP)
	   .Execute();
	sys.DelayUs(SMU_SETTLE_US);

	smu.Signal("SmuVdd").Disconnect();       // 注意：SMU 是 Disconnect，不是 DPS 的 DisConnect
	sys.DelayUs(SMU_SETTLE_US);
}

// 读一次电流（MEAS_AVG 已在硬件侧平均）
static double SMU_ReadCurrent()
{
	vector<ST_MEAS_RESULT> r;
	smu.Signal("SmuVdd").MeasureI(r, MEAS_AVG, 1, 10);
	if (r.empty()) {
		return 0.0;
	}
	return r[0].dbValue;
}

//------------------------------------------------------------------------------
// FN0  IDD_SMU_T —— 静态功耗
//
// 顺序很关键：先由 AWG 建立 E3 使能电平、再由 pattern 建立输入状态，
// 最后才给 VCC。反过来会让 CMOS 输入悬空，IDD 虚高几倍。
//------------------------------------------------------------------------------
USER_CODE void IDD_SMU_T() {
	TEST_BEGIN

	vector<ST_MEAS_RESULT> res;

	AWG_SetDcLevel("AWG_DC_EN", AWG_EN_HIGH);
	DIO_Setup();

	// Run + WaitEnd：只跑向量建立输入状态，不比较输出（本项不关心输出）
	dio.Pattern("AWG_EN_ON").SetTimeOut(20000).Run().WaitEnd();
	sys.DelayUs(5000);

	SMU_PowerOn(SMU_VCC_DEFAULT);
	res.push_back(MakeResult("IDD", SMU_ReadCurrent()));

	binObj.CheckResultAndBin(0, res, 1);

	SMU_PowerOff();
	DIO_Off();
	awg.Stop();
	sys.DelayUs(5000);

	TEST_ERROR
	binObj.HandlerException(0);
	TEST_END
}

//------------------------------------------------------------------------------
// FN1  IDD_SWEEP_SMU_T —— 供电电压扫描下的 IDD
//
// 目的：74HC138 标称 2~6V 工作，验证宽压范围内静态电流都合格，
//       同时能发现"某一电压点电流突增"这类工艺问题（如弱闩锁）。
// 参数顺序必须与 .tmf 的 ResultAnalysis 一致：2V0 / 3V3 / 5V0 / 5V5
//------------------------------------------------------------------------------
USER_CODE void IDD_SWEEP_SMU_T() {
	TEST_BEGIN

	vector<ST_MEAS_RESULT> res;
	const double vcc_list[4] = { 2.0, 3.3, 5.0, 5.5 };
	const char  *nm_list[4]  = { "IDD_2V0", "IDD_3V3", "IDD_5V0", "IDD_5V5" };

	AWG_SetDcLevel("AWG_DC_EN", AWG_EN_HIGH);
	DIO_Setup();
	dio.Pattern("AWG_EN_ON").SetTimeOut(20000).Run().WaitEnd();
	sys.DelayUs(5000);

	for (int i = 0; i < 4; ++i) {
		SMU_PowerOn(vcc_list[i]);
		res.push_back(MakeResult(nm_list[i], SMU_ReadCurrent()));
	}

	binObj.CheckResultAndBin(0, res, 4);

	SMU_PowerOff();
	DIO_Off();
	awg.Stop();
	sys.DelayUs(5000);

	TEST_ERROR
	binObj.HandlerException(0);
	TEST_END
}

//------------------------------------------------------------------------------
// FN2  VCC_IV_SMU_T —— VCC 脚 I-V 曲线
//
// 从 0V 缓慢升到 6V，记录每个点的电流。正常器件全程应是 µA 级平台；
// 若某个电压点之后电流陡增，说明存在闩锁或内部短路。
// 参数顺序：I_VCC_0V ... I_VCC_6V（共 7 点）
//
// ⚠️ 扫电压时不要反过来从高往低，也不要跳点，否则看不到拐点。
//------------------------------------------------------------------------------
USER_CODE void VCC_IV_SMU_T() {
	TEST_BEGIN

	vector<ST_MEAS_RESULT> res;
	const char *nm_list[7] = {
		"I_VCC_0V", "I_VCC_1V", "I_VCC_2V", "I_VCC_3V",
		"I_VCC_4V", "I_VCC_5V", "I_VCC_6V"
	};

	AWG_SetDcLevel("AWG_DC_EN", AWG_EN_HIGH);
	DIO_Setup();
	dio.Pattern("AWG_EN_ON").SetTimeOut(20000).Run().WaitEnd();
	sys.DelayUs(5000);

	for (int i = 0; i < 7; ++i) {
		SMU_PowerOn(static_cast<double>(i));      // 0V,1V,...,6V
		res.push_back(MakeResult(nm_list[i], SMU_ReadCurrent()));
	}

	binObj.CheckResultAndBin(0, res, 7);

	SMU_PowerOff();
	DIO_Off();
	awg.Stop();
	sys.DelayUs(5000);

	TEST_ERROR
	binObj.HandlerException(0);
	TEST_END
}

//------------------------------------------------------------------------------
// FN3  AWG_EN_ON_T —— AWG 给使能高电平，验证输出跟随
//
// AWG 给 E3 = 4.0V（> VIH），A0=A1=A2=0、E1=E2=0 → 应为 Y0=L、其余 H
// 这是"用 AWG 当激励源 + DIO 比较输出"的最小闭环。
//------------------------------------------------------------------------------
USER_CODE void AWG_EN_ON_T() {
	TEST_BEGIN

	vector<ST_DIO_RESULT> dioRes;

	AWG_SetDcLevel("AWG_DC_EN_HI", AWG_EN_HIGH);
	DIO_Setup();
	sys.DelayUs(5000);

	dio.Pattern("AWG_EN_ON").SetTimeOut(20000).Test();
	dio.Pattern("AWG_EN_ON").GetFailResult(dioRes);

	binObj.CheckResultAndBin(0, dioRes);

	DIO_Off();
	awg.Stop();
	sys.DelayUs(5000);

	TEST_ERROR
	binObj.HandlerException(0);
	TEST_END
}

//------------------------------------------------------------------------------
// FN4  AWG_EN_OFF_T —— AWG 给使能低电平，验证输出被禁能
//
// AWG 给 E3 = 0.5V（< VIL）→ 74HC138 全部输出应为高
// 与 FN3 配对，等于用 AWG 扫了两个电平点，验证使能脚的阈值行为。
//------------------------------------------------------------------------------
USER_CODE void AWG_EN_OFF_T() {
	TEST_BEGIN

	vector<ST_DIO_RESULT> dioRes;

	AWG_SetDcLevel("AWG_DC_DIS", AWG_EN_LOW);
	DIO_Setup();
	sys.DelayUs(5000);

	dio.Pattern("AWG_EN_OFF").SetTimeOut(20000).Test();
	dio.Pattern("AWG_EN_OFF").GetFailResult(dioRes);

	binObj.CheckResultAndBin(0, dioRes);

	DIO_Off();
	awg.Stop();
	sys.DelayUs(5000);

	TEST_ERROR
	binObj.HandlerException(0);
	TEST_END
}

//------------------------------------------------------------------------------
// FN5  SetPassBin
//------------------------------------------------------------------------------
USER_CODE void SetPassBin() {
	TEST_BEGIN

	binObj.SetBin(1);

	TEST_ERROR
	binObj.HandlerException(0);
	TEST_END
}

#include "interface.h"
#include <iostream>
#include <string>

using namespace std;
unsigned long SITE = 0, SITE_ID = 0, ACTIVE_SITE_NUM = 0;

USER_CODE void ProjectReuse() {
  cout << "ProjectReuse" << endl;
}

USER_CODE void ProjectInit() {
  cout << "ProjectInit" << endl;
}

USER_CODE void ProjectReset() {
  cout << "ProjectReset" << endl;
}

USER_CODE void site_init() {
  cout << "site_init" << endl;
}

USER_CODE void site_reset() {
  cout << "site_reset" << endl;
}

void EndOfWafer() {
  cout << "EndOfWafer" << endl;
}

void EndOfLot() {
  cout << "EndOfLot" << endl;
}

inline void Enable_EOW_Func() {
  proberObj.EnableWaferEndCheck(EndOfWafer);
}

inline void Enable_EOL_Func() {
  proberObj.EnableLotEndCheck(EndOfLot);
}

//==============================================================================
// OSN_T : 开短路测试 —— 下二极管（管脚 -> GND 方向）
// 原理：VCC 由 DPS 强置 0V（此时 VCC 与 GND 同为系统地），PPMU 从每个 IO 脚
//       抽 100uA 电流，电流路径为 GND -> 下二极管 -> 管脚 -> PPMU。
//       正常应读到 -0.2V ~ -1.2V（约 -0.65V）。
//       开路 -> 电流无回路 -> PPMU 打到负钳位 -2V -> FAIL
//       对 GND 短路 -> 0V -> FAIL（0V 不在 -1.2~-0.2 区间）
//==============================================================================
USER_CODE void OSN_T() {
	TEST_BEGIN

	vector<ST_MEAS_RESULT> OSN_RESULT;

	//初始化：VCC 接 DPS，14 个信号脚接 PPMU
	dps.Signal("VCC").Connect();
	ppmu.Signal("INOUT_PINS").Connect();
	sys.DelayUs(5000);

	//VCC 给 0V —— OS 测试的关键前提：不给芯片供电，
	//而是把 VCC 拉到地电位，为上二极管提供回路
	dps.Signal("VCC").SetMode("FVMV").VoltForce(0)
									 .CurrRange(0.5)
									 .CurrClamp(0.5, -0.5)
									 .Execute();
	sys.DelayUs(5000);

	//INOUT_PINS 先全部置 0V，泄放残余电荷，保证每个脚同起始态
	ppmu.Signal("INOUT_PINS").SetMode("FVMV").VoltForce(0)
											 .CurrRange(40e-3)
											 .Execute();
	sys.DelayUs(5000);

	//并测方式从芯片的所有信号管脚上抽 100uA 电流
	ppmu.Signal("INOUT_PINS").SetMode("FIMV").CurrForce(-100e-6)
											 .CurrRange(100e-6)
											 .VoltClamp(2, -2)
											 .Execute();
	sys.DelayUs(5000);

	//稳定后测量电压值
	ppmu.Signal("INOUT_PINS").Measure(OSN_RESULT);

	//和 TMF 文件中设置的阈值进行比较（14 个管脚）
	binObj.CheckResultAndBin(0, OSN_RESULT, 14);

	//复位
	dps.Signal("VCC").SetMode("FVMV").VoltForce(0)
									 .CurrRange(0.5)
									 .CurrClamp(0.5, -0.5)
									 .Execute();
	sys.DelayUs(5000);
	ppmu.Signal("INOUT_PINS").SetMode("FVMV").VoltForce(0)
											 .CurrRange(40e-3)
											 .Execute();
	sys.DelayUs(5000);
	dps.Signal("VCC").DisConnect();
	ppmu.Signal("INOUT_PINS").DisConnect();
	sys.DelayUs(5000);

	TEST_ERROR
	binObj.HandlerException(0);
	TEST_END
}

//==============================================================================
// OSP_T : 开短路测试 —— 上二极管（VCC -> 管脚 方向）
// 原理：VCC 仍强置 0V，PPMU 向每个 IO 脚灌 100uA 电流，
//       电流路径为 PPMU -> 管脚 -> 上二极管 -> VCC(0V) -> GND。
//       正常应读到 0.2V ~ 1.2V（约 +0.65V）。
//==============================================================================
USER_CODE void OSP_T() {
	TEST_BEGIN

	vector<ST_MEAS_RESULT> OSP_RESULT;

	//初始化
	dps.Signal("VCC").Connect();
	ppmu.Signal("INOUT_PINS").Connect();
	sys.DelayUs(5000);

	//VCC 给 0V
	dps.Signal("VCC").SetMode("FVMV").VoltForce(0)
									 .CurrRange(0.5)
									 .CurrClamp(0.5, -0.5)
									 .Execute();
	sys.DelayUs(5000);

	//INOUT_PINS 先全部置 0V
	ppmu.Signal("INOUT_PINS").SetMode("FVMV").VoltForce(0)
											 .CurrRange(40e-3)
											 .Execute();
	sys.DelayUs(5000);

	//并测方式向芯片的所有信号管脚上灌 100uA 电流
	ppmu.Signal("INOUT_PINS").SetMode("FIMV").CurrForce(100e-6)
											 .CurrRange(100e-6)
											 .VoltClamp(2, -2)
											 .Execute();
	sys.DelayUs(5000);

	//稳定后测量电压值
	ppmu.Signal("INOUT_PINS").Measure(OSP_RESULT);

	//和 TMF 文件中设置的阈值进行比较（14 个管脚）
	binObj.CheckResultAndBin(0, OSP_RESULT, 14);

	//复位
	dps.Signal("VCC").SetMode("FVMV").VoltForce(0)
									 .CurrRange(0.5)
									 .CurrClamp(0.5, -0.5)
									 .Execute();
	sys.DelayUs(5000);
	ppmu.Signal("INOUT_PINS").SetMode("FVMV").VoltForce(0)
											 .CurrRange(40e-3)
											 .Execute();
	sys.DelayUs(5000);
	dps.Signal("VCC").DisConnect();
	ppmu.Signal("INOUT_PINS").DisConnect();
	sys.DelayUs(5000);

	TEST_ERROR
	binObj.HandlerException(0);
	TEST_END
}

USER_CODE void SetPassBin() {
	TEST_BEGIN

	// TODO Edit your code here
	binObj.SetBin(1);

	TEST_ERROR
	binObj.HandlerException(0);
	TEST_END
}

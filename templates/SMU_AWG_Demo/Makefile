RUNNER_HOME = d:/MINI-ATE\runner
CFGOPT = -shared -Wall -Wl,-subsystem,windows -mthreads -fPIC
LIBS = -lsetupapi -lkernel32 -luser32 -lgdi32 -lwinspool -llibCommon -lcomdlg32 -ladvapi32 -lshell32 -lole32 -loleaut32 -luuid -lodbc32 -lodbccp32 \
		-L$(RUNNER_HOME) -lproject -lTester -lBinDef -lUsrApi -lWaveCalc -lShmoo
INCS = -I./ -I$(RUNNER_HOME)/inc/ \

SRC = Test.cpp \

TESTLIB = Test.dll
ifeq ($(RUNNER_HOME),)
$(error RUNNER_HOME not set in environment)
endif
all: | $(RUNNER_HOME)
	g++ -g -std=c++17 $(SRC) $(INCS) $(LIBS) $(CFGOPT) -DINTERFACE_EXPORTS -o $(TESTLIB)
clean: $(TESTLIB)
	$(RM) $(TESTLIB)
$(RUNNER_HOME):
	$(error Folder $(RUNNER_HOME) does not exist)

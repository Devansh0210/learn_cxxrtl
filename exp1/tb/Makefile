# To build single-core dual-port tb: make
# To build dual-core single-port tb: make DOTF=tb_multicore.f

TOP         := tb
DOTF        := tb.f
CONFIG      := default
TBEXEC      := $(patsubst %.f,%,$(DOTF))
SCRIPTS     := /home/a0500701/proj/fpgascripts

FILE_LIST   := $(shell $(SCRIPTS)/listfiles $(DOTF))
BUILD_DIR   := build-$(patsubst %.f,%,$(DOTF))
CXX_RTL_INC := /home/a0500701/oss-cad-suite/share/yosys/include/backends/cxxrtl/runtime

# Note: clang++-18 has a >20x compile time regression, even at low
# optimisation levels. I have tried clang++-16 and clang++-17, both fine.
GXX   := g++

.PHONY: clean all lint

all: $(TBEXEC)

SYNTH_CMD += read_verilog -I ../../../hdl -DCONFIG_HEADER="config_$(CONFIG).vh" $(FILE_LIST);
SYNTH_CMD += hierarchy -top $(TOP);
SYNTH_CMD += write_cxxrtl $(BUILD_DIR)/dut.cpp

$(BUILD_DIR)/dut.cpp: $(FILE_LIST) $(wildcard *.vh)
	mkdir -p $(BUILD_DIR)
	yosys -p '$(SYNTH_CMD)' 2>&1 > $(BUILD_DIR)/cxxrtl.log

clean::
	rm -rf $(BUILD_DIR) $(TBEXEC)

$(TBEXEC): $(BUILD_DIR)/dut.cpp tb.cpp
	$(GXX) -O3 -std=c++14 $(addprefix -D,$(CDEFINES) $(CDEFINES_$(DOTF))) -I$(CXX_RTL_INC) -I$(BUILD_DIR) tb.cpp -o $(TBEXEC)

lint:
	verilator --lint-only --top-module $(TOP) -I$(HDL) $(FILE_LIST)


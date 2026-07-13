################################################################################
# BMS functional-safety self-test sources (maintained manually).
################################################################################

BMS_SELFTEST_SRC := ../../../../vendor/ble_sample/bms_selftest
BMS_SELFTEST_INC := -I".." -I"../../../.." -I"../../../../vendor/common" -I"../../../../vendor/ble_sample" -I"../../../../vendor/ble_sample/bms_selftest" -I"../../../../common" -I"../../../../drivers/B85"
BMS_SELFTEST_CFLAGS := -ffunction-sections -fdata-sections $(BMS_SELFTEST_INC) -D__PROJECT_8258_BLE_SAMPLE__=1 -DCHIP_TYPE=CHIP_TYPE_825x -DBMS_DIAG_TEST_BUILD=$(BMS_TEST_BUILD) -DBMS_DIAG_FAULT_INJECT_ENABLE=$(BMS_FAULT_INJECT_ENABLE) -DBMS_FAULT_INJECT_MASK=$(BMS_FAULT_INJECT_MASK) -Wall -O2 -fshort-enums -finline-small-functions -std=gnu99 -fshort-wchar -fms-extensions

C_SRCS += \
$(BMS_SELFTEST_SRC)/bms_diag.c \
$(BMS_SELFTEST_SRC)/bms_failsafe.c \
$(BMS_SELFTEST_SRC)/bms_fault_inject.c \
$(BMS_SELFTEST_SRC)/bms_selftest.c \
$(BMS_SELFTEST_SRC)/bms_selftest_application.c \
$(BMS_SELFTEST_SRC)/bms_selftest_clock.c \
$(BMS_SELFTEST_SRC)/bms_selftest_controlflow.c \
$(BMS_SELFTEST_SRC)/bms_selftest_cpu.c \
$(BMS_SELFTEST_SRC)/bms_selftest_flash.c \
$(BMS_SELFTEST_SRC)/bms_selftest_interrupt.c \
$(BMS_SELFTEST_SRC)/bms_selftest_ram.c \
$(BMS_SELFTEST_SRC)/bms_selftest_stack.c \
$(BMS_SELFTEST_SRC)/bms_selftest_port_telink.c

S_UPPER_SRCS += $(BMS_SELFTEST_SRC)/bms_selftest_cpu_tc32.S

OBJS += \
./vendor/ble_sample/bms_selftest/bms_diag.o \
./vendor/ble_sample/bms_selftest/bms_failsafe.o \
./vendor/ble_sample/bms_selftest/bms_fault_inject.o \
./vendor/ble_sample/bms_selftest/bms_selftest.o \
./vendor/ble_sample/bms_selftest/bms_selftest_application.o \
./vendor/ble_sample/bms_selftest/bms_selftest_clock.o \
./vendor/ble_sample/bms_selftest/bms_selftest_controlflow.o \
./vendor/ble_sample/bms_selftest/bms_selftest_cpu.o \
./vendor/ble_sample/bms_selftest/bms_selftest_flash.o \
./vendor/ble_sample/bms_selftest/bms_selftest_interrupt.o \
./vendor/ble_sample/bms_selftest/bms_selftest_ram.o \
./vendor/ble_sample/bms_selftest/bms_selftest_stack.o \
./vendor/ble_sample/bms_selftest/bms_selftest_port_telink.o \
./vendor/ble_sample/bms_selftest/bms_selftest_cpu_tc32.o

vendor/ble_sample/bms_selftest/%.o: $(BMS_SELFTEST_SRC)/%.c
	@echo 'Building self-test file: $<'
	tc32-elf-gcc $(BMS_SELFTEST_CFLAGS) -c -o"$@" "$<"

vendor/ble_sample/bms_selftest/bms_selftest_cpu_tc32.o: $(BMS_SELFTEST_SRC)/bms_selftest_cpu_tc32.S
	@echo 'Building TC32 self-test assembly: $<'
	tc32-elf-gcc -c -o"$@" "$<"

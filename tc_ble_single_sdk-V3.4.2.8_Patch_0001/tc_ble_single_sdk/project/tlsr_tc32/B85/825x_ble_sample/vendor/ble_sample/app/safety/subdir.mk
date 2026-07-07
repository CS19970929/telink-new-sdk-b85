################################################################################
# Safety framework build fragment.
################################################################################

C_SRCS += \
../../../../vendor/ble_sample/app/safety/safety_adc_check.c \
../../../../vendor/ble_sample/app/safety/safety_clock_test.c \
../../../../vendor/ble_sample/app/safety/safety_comm_check.c \
../../../../vendor/ble_sample/app/safety/safety_cpu_test.c \
../../../../vendor/ble_sample/app/safety/safety_fault.c \
../../../../vendor/ble_sample/app/safety/safety_flash_crc.c \
../../../../vendor/ble_sample/app/safety/safety_flow_check.c \
../../../../vendor/ble_sample/app/safety/safety_manager.c \
../../../../vendor/ble_sample/app/safety/safety_mos_check.c \
../../../../vendor/ble_sample/app/safety/safety_ram_test.c \
../../../../vendor/ble_sample/app/safety/safety_runtime.c \
../../../../vendor/ble_sample/app/safety/safety_startup.c \
../../../../vendor/ble_sample/app/safety/safety_watchdog.c

S_UPPER_SRCS += \
../../../../vendor/ble_sample/app/safety/safety_cpu_test_asm.S

OBJS += \
./vendor/ble_sample/app/safety/safety_adc_check.o \
./vendor/ble_sample/app/safety/safety_clock_test.o \
./vendor/ble_sample/app/safety/safety_comm_check.o \
./vendor/ble_sample/app/safety/safety_cpu_test.o \
./vendor/ble_sample/app/safety/safety_cpu_test_asm.o \
./vendor/ble_sample/app/safety/safety_fault.o \
./vendor/ble_sample/app/safety/safety_flash_crc.o \
./vendor/ble_sample/app/safety/safety_flow_check.o \
./vendor/ble_sample/app/safety/safety_manager.o \
./vendor/ble_sample/app/safety/safety_mos_check.o \
./vendor/ble_sample/app/safety/safety_ram_test.o \
./vendor/ble_sample/app/safety/safety_runtime.o \
./vendor/ble_sample/app/safety/safety_startup.o \
./vendor/ble_sample/app/safety/safety_watchdog.o

vendor/ble_sample/app/safety/%.o: ../../../../vendor/ble_sample/app/safety/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: TC32 Compiler'
	tc32-elf-gcc -ffunction-sections -fdata-sections -I".." -I"../../../.." -I"../../../../vendor/ble_sample" -I"../../../../vendor/common" -I"../../../../common" -I"../../../../drivers/B85" -D__PROJECT_8258_BLE_SAMPLE__=1 -DCHIP_TYPE=CHIP_TYPE_825x -Wall -O2 -fpack-struct -fshort-enums -finline-small-functions -std=gnu99 -fshort-wchar -fms-extensions -c -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

vendor/ble_sample/app/safety/%.o: ../../../../vendor/ble_sample/app/safety/%.S
	@echo 'Building file: $<'
	@echo 'Invoking: TC32 CC/Assembler'
	tc32-elf-gcc -DMCU_STARTUP_8258 -c -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

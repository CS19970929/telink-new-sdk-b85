################################################################################
# Safety modules (kept separate from the IDE-generated subdir.mk).
################################################################################

BMS_SAFETY_SRC_DIR := ../../../../vendor/ble_sample
BMS_SAFETY_NAMES := bms_actuator bms_afe bms_diagnostics bms_fet_monitor \
	bms_fuse bms_measurement bms_safety bms_safety_log bms_supervisor bms_sw_protection

C_SRCS += $(addprefix $(BMS_SAFETY_SRC_DIR)/,$(addsuffix .c,$(BMS_SAFETY_NAMES)))
OBJS += $(addprefix ./vendor/ble_sample/,$(addsuffix .o,$(BMS_SAFETY_NAMES)))

BMS_SAFETY_CFLAGS := -ffunction-sections -fdata-sections \
	-I".." -I"../../../.." -I"../../../../vendor/common" \
	-I"../../../../common" -I"../../../../drivers/B85" \
	-D__PROJECT_8258_BLE_SAMPLE__=1 -DCHIP_TYPE=CHIP_TYPE_825x \
	-Wall -O2 -fpack-struct -fshort-enums -finline-small-functions \
	-std=gnu99 -fshort-wchar -fms-extensions

./vendor/ble_sample/bms_%.o: $(BMS_SAFETY_SRC_DIR)/bms_%.c
	@echo 'Building safety file: $<'
	tc32-elf-gcc $(BMS_SAFETY_CFLAGS) -c -o"$@" "$<"

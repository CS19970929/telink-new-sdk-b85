################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_UPPER_SRCS += \
D:/telink/tc_ble_single_sdk-V3.4.2.8_Patch_0001\ (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001\ (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/boot/B85/cstartup_825x.S 

OBJS += \
./boot/B85/cstartup_825x.o 


# Each subdirectory must supply rules for building sources it contributes
boot/B85/cstartup_825x.o: D:/telink/tc_ble_single_sdk-V3.4.2.8_Patch_0001\ (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001\ (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/boot/B85/cstartup_825x.S
	@echo 'Building file: $<'
	@echo 'Invoking: TC32 CC/Assembler'
	tc32-elf-gcc -DMCU_STARTUP_8258 -c -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '




d008没有开关，

MCU_LDO_PIN低电平断电，唤醒了？待测试

rtc增加充电电流、放电电流500ms以上，soc校准


runtime模块是否有用，后续改到其他功能

MCC-EN-RF  控制加热回路的保险丝？

RF_EN_PIN

SW_PIN改为ACC-MCU，类似开关，目前不写逻辑吧

bms_afe_set_output_enabled

CHG_IN_PIN 负载检测，暂时不写逻辑

bms_afe_guard.c 是否有用


s_project_config_pending

GPIO_PD7  MCU-AFE-EN 是否有用，测试afe shutdown功耗


gp1测加热温度
gp2、gp3 电池温度
gp4 mos温度

电池温度用于充电、放电高低温保护，以及用于加热逻辑，gp1的温度是测量加热mos附近温度，如果加热mos明面没打哭，却温度太高，代表电路异常，需要PD4（MCC-EN-RF）给高，熔断加热回路保险丝，保证电池温度不能太高。gp4的mos温度用于mos温度保护，mos温度只有高温保护，低温不需要保护





目前只要温度达到对应温度，就会保护，需要修改为对应充、放电，有电流时同时温度达到对应保护温度，才触发保护。


ACC_MCU_PIN先作为开关，检测到低电平时，bms保持正常运行，检测到高电平时，bms休眠，但这个休眠和目前其他休眠要区分，这个休眠只让afe shutdown和mcu深度休眠，而不通过MCU_LDO_PIN断电，然后休眠后可以通过ACC_MCU_PIN唤醒，ACC_MCU_PIN低电平唤醒。
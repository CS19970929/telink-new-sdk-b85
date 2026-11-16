# Comm Tool App

目标 STM32F103RET6，沿用 STM32F10x StdPeriph；App和IAP由同一Keil工程的两个target构建。
[当前开发、布局、协议与维护入口](../../../../docs/comm-tool/README.md)。
ct_owc.*负责主循环解码，board_owc.*负责PC11边沿与PC10上拉；其他模块沿用CAN、串口桥接与升级流程。

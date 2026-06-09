# TODO

## Pending items

- [ ] item 1
- [ ] item 2
- [ ] item 3

## Notes

- `- [ ]` means unfinished and will trigger a reminder before `git push`
- `- [x]` means completed and will not trigger a reminder


深度休眠逻辑（小于100uA）：
1、单节电压低于2750并且无充电，持续12小时，进入深度休眠，需要充电激活。
2、单节电压低于3000并且无充电，持续48小时，进入深度休眠，需要充电激活。
2、单节电压低于2550并且无充电，持续48小时，进入深度休眠，需要充电激活。
3、开关断开，进入休眠，开关闭合或充电可激活。

方案 A：临时跳过 hook，先 push
git push --no-verify


单独开一个分支，以ble_sample为参考，结合新afe sh3673520实现20串，蓝牙保护板，给你提供afe的参考文档，新afe是spi通信，具体见文档

新项目需要完全重构，afe参数需要可以读写，使用内部flash，需要实现配套的上位机，具备蓝牙和串口连接方式

需要当前项目信息的完整文档，modbus地址、蓝牙通信参数、afe驱动、afe通信、afe寄存器，flash分区、afe参数读写等等
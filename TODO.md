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
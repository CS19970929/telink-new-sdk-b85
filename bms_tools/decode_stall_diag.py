"""离线解码0x3A00的160个word；不连接设备、不写寄存器。

输入为160个整数的JSON数组，或仅含320字节大端寄存器数据的.bin文件
（不含RTU地址/功能码/长度/CRC）；结果打印到stdout，原始word一并保留。
"""
import argparse
import json
from pathlib import Path

PHASES = (
    'BOOT', 'LOOP', 'SDK_ENTER', 'SDK_RETURN', 'UART', 'SAMPLE_BEGIN',
    'SAMPLE_END', 'STORAGE', 'PROTOCOL', 'POWER', 'PM_ENTER', 'PM_RETURN',
    'APP_WAKE', 'DEEP_COMMIT', 'BOOT_HOLD',
)
REASONS = ('NONE', 'PHASE', 'SDK', 'PM', 'RAW_32K', 'SAMPLE')
SLEEP_REASONS = (
    'NONE', 'COMMAND', 'ACC', 'SWITCH', 'VERY_LOW_RESERVED', 'LOW', 'NORMAL',
    'AFE', 'OVERDISCHARGE',
)
FIELDS = (
    'phase', 'phase_tick', 'loops', 'sdk_returns', 'samples', 'sample_begin',
    'sample_end', 'sdk_enter', 'sdk_return', 'app_wakes', 'pm_enters',
    'pm_returns', 'pm_enter', 'pm_return', 'pm_mode', 'pm_sources',
    'pm_deadline', 'pm_result', 'raw_32k', 'sample_due', 'sample_deadline',
    'irq_enters', 'irq_exits', 'irq_source', 'timer_regs', 'recover_flag',
    'afe_events', 'afe_last', 'guard_bits', 'guard_elapsed_ms', 'guard_wait_ms', 'detail',
)


def name(names, value):
    return names[value] if value < len(names) else f'UNKNOWN({value})'


def decode(words):
    if (not isinstance(words, list) or len(words) != 160 or
            any(type(w) is not int or not 0 <= w <= 65535 for w in words)):
        raise ValueError('必须为160个0..65535整数，按0x3A00地址递增排列')
    if words[0] != 0x5354 or words[1] not in (1, 2) or words[3] != 160 or words[2] not in (0, 1):
        raise ValueError('magic/schema/长度/enabled不匹配')

    def u32(offset):
        return (words[offset] << 16) | words[offset + 1]

    def snapshot(offset):
        result = {field: u32(offset + 2 * i) for i, field in enumerate(FIELDS)}
        result['phase_name'] = name(PHASES, result['phase'])
        # 参数属于最近一次调用，返回值可能还是上一次；次数配对后才解释本次返回。
        paired = result['pm_enters'] != 0 and result['pm_enters'] == result['pm_returns']
        raw = result['pm_result']
        result['pm_return_flags'] = {
            'sdk_enter_suspend_bit': bool(raw & 0x40000000),
            'gpio_no_enter_bit': bool(raw & 0x100),
            'timer': bool(raw & 2), 'core': bool(raw & 4), 'pad': bool(raw & 8),
        } if paired else None
        return result

    afe = [u32(144 + 2 * i) for i in range(8)]
    error = afe[0] & 0xFFFF
    deep_marker = words[13] if words[1] >= 2 else 0
    deep_reason = deep_marker & 15
    deep_valid = ((deep_marker & 0xFFF0) == 0xD0 and
                  (deep_reason < len(SLEEP_REASONS) or deep_reason in (14, 15)))
    deep_name = ('BOOT_HOLD' if deep_reason == 15 else
                 ('UNATTRIBUTED' if deep_reason == 14 else name(SLEEP_REASONS, deep_reason)))
    result = {
        'schema': words[1], 'enabled': bool(words[2]), 'build_id': f'{u32(6):08x}',
        'boot_counter_previous': words[8] >> 8,
        'boot_counter_current': words[8] & 0xFF,
        'boot_raw_0x72': words[9], 'boot_ana_reg0': words[10],
        'boot_ana_reg6': words[11], 'boot_wakeup_source': words[12],
        'boot_ana_reg7': deep_marker,
        'previous_deep_call_marker_valid': deep_valid,
        'previous_deep_call_reason': deep_name if deep_valid else None,
        'first_gap_reason': name(REASONS, words[4]),
        'resume_stage': name(PHASES, words[5]), 'first_gap_ms': u32(14),
        'live': snapshot(16), 'first_gap': snapshot(80),
        'first_afe': {
            'stage': afe[0] >> 16, 'error': error - 65536 if error & 0x8000 else error,
            'tick': afe[1], 'raw_32k_cached': afe[2], 'valid_fields': afe[3] >> 16,
            'flag1': (afe[3] >> 8) & 255, 'flag2': afe[3] & 255,
            'crc_errors': afe[4], 'retries': afe[5], 'guard_bits': afe[6],
            'phase': name(PHASES, afe[7]),
        },
        'raw_words': words,
        'limits': '首次长间隔表示观测边界，不是芯片唯一根因；RAM复位丢失，计数8位回绕且掉电清零。分段读取须用末次header确认同一启动。',
    }
    return result


def compare_boot(current, baseline):
    """只给证据结论；启动计数包含DeepSleep唤醒，不直接等同异常复位。"""
    if not current['enabled'] or not baseline['enabled']:
        return ['诊断未启用，不能比较运行现场。']
    if current['build_id'] != baseline['build_id']:
        return ['两份数据build不同，不能作为同一固件故障的前后对照。']
    notes = []
    if current['boot_counter_current'] != baseline['boot_counter_current']:
        notes.append('启动计数变化：期间重新进入main；可能是复位或DeepSleep唤醒，不能直接等同异常复位。')
        if current['boot_raw_0x72'] & 1:
            notes.append('WDT标志两次均为1，不能用粘滞旧值判定本次原因。' if baseline['boot_raw_0x72'] & 1 else
                         'WDT标志相对基线由0变1，支持期间发生过WDT复位；不保证它是最后一次启动原因。')
        if (current['boot_ana_reg0'] & 2) and not (baseline['boot_ana_reg0'] & 2):
            notes.append('SDK晶振故障标志相对基线由0变1，支持期间走过晶振故障路径，需结合SDK现场。')
        if current['previous_deep_call_marker_valid']:
            notes.append('本次启动捕获DeepSleep调用标记，原因=' + current['previous_deep_call_reason'] +
                         '；支持DeepSleep唤醒链，仍须结合启动PAD状态和实际硬件。')
        else:
            notes.append('没有有效DeepSleep调用标记；复位/掉电、标记丢失或旧schema均需继续判别。')
        return notes
    notes.append('启动计数未变化，支持原现场续跑；掉电后数值碰撞和256次回绕仍不能排除。')
    if current['first_gap_reason'] == 'PM' and current['first_gap']['pm_mode'] == 0:
        notes.append('捕获跨普通Suspend驱动的长间隔；不能仅由此区分物理睡眠与驱动内部等待。')
        flags = current['first_gap']['pm_return_flags']
        if flags and flags['sdk_enter_suspend_bit']:
            notes.append('SDK返回含bit30进入标记；支持经过正常入睡/唤醒返回路径，但不说明整个长间隔都在物理Suspend。')
        elif flags:
            notes.append('SDK返回没有bit30进入标记；不能把mode=Suspend直接解释为已经物理入睡，需检查提前返回/等待路径。')
    elif current['first_gap_reason'] == 'SDK':
        notes.append('捕获SDK调用长间隔；结合冻结阶段、PM进入/返回次数定位。')
    else:
        notes.append('结合首次长间隔及AFE现场定位；首次冻结可能早于用户观察到的故障。')
    return notes


def load_words(path):
    if path.suffix.lower() == '.bin':
        data = path.read_bytes()
        if len(data) != 320:
            raise ValueError('二进制输入必须恰为320字节寄存器数据')
        return [int.from_bytes(data[i:i+2], 'big') for i in range(0, 320, 2)]
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--baseline', type=Path, help='同一诊断固件正常运行时保存的160word基线')
    args = parser.parse_args()
    try:
        result = decode(load_words(args.input))
        if args.baseline:
            result['boot_comparison'] = compare_boot(result, decode(load_words(args.baseline)))
        print(json.dumps(result, ensure_ascii=False, indent=2))
    except (OSError, ValueError) as exc:
        parser.error(str(exc))


if __name__ == '__main__':
    main()

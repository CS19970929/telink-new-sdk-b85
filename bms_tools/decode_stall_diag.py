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
    'APP_WAKE', 'DEEP_COMMIT',
)
REASONS = ('NONE', 'PHASE', 'SDK', 'PM', 'RAW_32K', 'SAMPLE')
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
    if words[:2] != [0x5354, 1] or words[3] != 160 or words[2] not in (0, 1):
        raise ValueError('magic/schema/长度/enabled不匹配')

    def u32(offset):
        return (words[offset] << 16) | words[offset + 1]

    def snapshot(offset):
        result = {field: u32(offset + 2 * i) for i, field in enumerate(FIELDS)}
        result['phase_name'] = name(PHASES, result['phase'])
        return result

    afe = [u32(144 + 2 * i) for i in range(8)]
    error = afe[0] & 0xFFFF
    result = {
        'enabled': bool(words[2]), 'build_id': f'{u32(6):08x}',
        'boot_counter_previous': words[8] >> 8,
        'boot_counter_current': words[8] & 0xFF,
        'boot_raw_0x72': words[9], 'boot_ana_reg0': words[10],
        'boot_ana_reg6': words[11], 'boot_wakeup_source': words[12],
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
        'limits': '首次长间隔表示观测边界，不是芯片唯一根因；RAM复位丢失，计数8位回绕且掉电清零。',
    }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    args = parser.parse_args()
    try:
        if args.input.suffix.lower() == '.bin':
            data = args.input.read_bytes()
            if len(data) != 320:
                raise ValueError('二进制输入必须恰为320字节寄存器数据')
            words = [int.from_bytes(data[i:i+2], 'big') for i in range(0, 320, 2)]
        else:
            words = json.loads(args.input.read_text(encoding='utf-8-sig'))
        print(json.dumps(decode(words), ensure_ascii=False, indent=2))
    except (OSError, ValueError) as exc:
        parser.error(str(exc))


if __name__ == '__main__':
    main()

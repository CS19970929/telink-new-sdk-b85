"""源码 AFE 字段与保护参数的换算；不另存一份原始保护覆盖表。"""
from fractions import Fraction
import re

SH_OV_DELAY = (140, 280, 490, 980, 2030, 3010, 4970, 10010)
SH_UV_DELAY = (490, 770, 980, 1470, 2030, 3010, 4970, 10010)
SH_SC_DELAY = (0, 32, 64, 96, 128, 192, 224, 256, 288, 320, 384, 448, 480, 512, 544, 576)
DVC_V_DELAY = (200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000)


def ceiling(value, table):
    return next((i for i, v in enumerate(table) if value <= v), len(table) - 1)


def floor_index(value, table):
    return max((i for i, v in enumerate(table) if value >= v), default=0)


def ceil_fraction(value):
    return -(-value.numerator // value.denominator)


def field_help(field):
    """去掉索引内部 JSON/路径噪声，保留原厂编码和跨字节约束。"""
    lines = [field['description']]
    def encoding(value):
        if isinstance(value, list):
            for item in value: encoding(item)
        elif isinstance(value, dict):
            if 'code_text' in value:
                lines.append(str(value['code_text']) + '：' + value.get('meaning_zh', ''))
            elif 'values' in value:
                lines.extend(f"{i}：{v} {value.get('unit', '')}" for i, v in enumerate(value['values']))
            elif 'scale' in value:
                lines.append(f"完整逻辑值 = 编码 × {value['scale']} + {value.get('offset', 0)} {value.get('unit', '')}")
    encoding(field.get('encoding'))
    if field.get('encoding_input'): lines.append(field['encoding_input'])
    if field.get('combine_code'): lines.append('跨字节组合：' + field['combine_code'])
    for name in ('read_side_effect','write_side_effect','write_1_behavior'):
        if field.get(name): lines.append(name+'：'+str(field[name]))
    return '\n'.join(line for line in lines if line)


class SourceProtectionFields:
    def __init__(self, config, element=None, parallel=None):
        self.config = config
        self.dvc = config.product == 'd008'
        self.shunt = Fraction(config.element if element is None else element, config.parallel if parallel is None else parallel)
        if self.shunt<=0: raise ValueError('分流器比例必须大于零')
        self.rows = {r['name']: r for r in config.rows}
        # 温度阈值沿真实生产 NTC 表插值；不将其它 B 值替换为这一张表。
        self.ntc = None

    def row(self, name):
        full = ('D008_AFE_' if self.dvc else 'SH3673510_HW_DEFAULT_') + name
        return self.rows.get(full + '_FOR') or self.rows[full]

    def value(self, name):
        return int(self.row(name)['edit'])

    def set(self, name, value):
        if not 0 <= value <= 65535: raise ValueError('该编码超出源码参数的 16 位表示范围')
        self.row(name)['edit'] = str(value)

    def temp_code(self, temp, low):
        if self.ntc is None:
            text = (self.config.root / 'bms/afe/sh3673510/sh3673510_ntc.c').read_text(encoding='utf8')
            table = text[text.index('{') + 1:text.index('}')]
            values = [int(v) for v in re.findall(r'(\d+)u', table)]
            self.ntc = list(zip(values[::2], values[1::2]))
        r = self.ntc[0][0] if temp <= self.ntc[0][1] else self.ntc[-1][0]
        for (r1, t1), (r2, t2) in zip(self.ntc, self.ntc[1:]):
            if t1 <= temp <= t2:
                r = r1 - ((r1 - r2) * (temp - t1) + (t2 - t1) // 2) // (t2 - t1)
                break
        if low:
            if r < 100: return None
            return ((r - 100) * 256 + (r + 100) // 2) // (r + 100)
        code = (r * 512 + (r + 100) // 2) // (r + 100)
        return code if code <= 255 else None

    def current_code(self, name, step, maximum, up=True):
        value = Fraction(self.value(name)) * self.shunt / (10 * step)
        count = ceil_fraction(value) if up else value.numerator // value.denominator
        return max(1, min(maximum + 1, count)) - 1

    def sh_write_validation_errors(self, chemistry):
        """SH 完整语义参数按在线严格写入规则检查；不修改旧值或替用户选阈值。

        依据 SH CV1.0A 原 PDF p34/36..39/49 与
        bms_afe_hw_profile_validate()/validate_write()。schema/model 不是源码表的编辑项。
        """
        if self.dvc:
            return []
        errors = []
        def value(name):
            row = self.row(name)
            return row['edits'][chemistry] if row['ternary'] else int(row['edit'])
        def enabled(name):
            return value('ENABLE_' + name) != 0
        def require(condition, message):
            if not condition:
                errors.append(f'{chemistry.upper()} / {message}')
        for name, minimum, maximum in (('COV', 3000, 4500), ('CUV', 1000, 3500)):
            if enabled(name):
                require(minimum <= value(name + '_MV') <= maximum,
                        f'{name}_MV={value(name + "_MV")} mV，允许 {minimum}..{maximum} mV')
                trip, recover = value(name + '_MV'), value(name + '_RECOVER_MV')
                require(recover < trip if name == 'COV' else recover > trip,
                        f'{name}_RECOVER_MV 必须' + ('低于' if name == 'COV' else '高于') + '保护电压')
        for name, minimum, maximum in (('COV', 140, 10010), ('CUV', 490, 10010),
                ('OCD1', 140, 10010), ('OCD2', 25, 400), ('OCC1', 140, 10010), ('SC', 0, 576)):
            field = name + ('_DELAY_US' if name == 'SC' else '_DELAY_MS')
            unit = 'us' if name == 'SC' else 'ms'
            minimum = minimum if enabled(name) else 0
            require(minimum <= value(field) <= maximum,
                    f'{field}={value(field)} {unit}，允许 {minimum}..{maximum} {unit}')
        require(enabled('OCD1') == enabled('OCD2'), 'OCD1/OCD2 共用硬件使能，必须同时开或关')
        for name, step, count, recover in (('OCD1', 5000, 16, 'OCD'),
                ('OCD2', 10000, 16, 'OCD'), ('OCC1', 1375, 32, 'OCC')):
            if not enabled(name):
                continue
            sense = Fraction(value(name + '_A10')) * self.shunt / 10
            minimum = ceil_fraction(Fraction(step * 10) / self.shunt)
            maximum = min(65535, int(Fraction(step * count * 10) / self.shunt))
            require(step <= sense <= step * count,
                    f'{name}_A10={value(name + "_A10")}，允许 {minimum}..{maximum}（0.1 A）')
            steps = max(1, min(count, ceil_fraction(sense / step)))
            actual = min(65535, ceil_fraction(Fraction(steps * step * 10) / self.shunt))
            require(value(recover + '_RECOVER_A10') < actual,
                    f'{recover}_RECOVER_A10 必须低于 {name} 量化后的有效电流 {actual}（0.1 A）')
        if enabled('SC'):
            sense = Fraction(value('OCD2_A10')) * self.shunt / 10
            require(10000 <= sense <= 160000, 'SC 依赖 OCD2，OCD2 基准必须落在 10..160 mV')
            base = Fraction(max(1, min(16, ceil_fraction(sense / 10000))) * 100000) / self.shunt
            requested = value('SC_A10')
            require(2 * base <= requested <= 6 * base, 'SC_A10 必须落在量化后 OCD2 基准的 2..6 倍内')
            multiple = next((m for m in (2, 3, 4, 6) if requested <= m * base), 6)
            require(base * multiple <= 65535, 'SC 量化后的有效电流超出 16 位 0.1 A 报告范围')
        for name in ('CHG_OT', 'CHG_UT', 'DSG_OT', 'DSG_UT'):
            for suffix in ('_X10', '_RECOVER_X10'):
                field = name + suffix
                require(0 <= value(field) <= 1450, f'{field} 超出协议温度范围 −40..105℃')
        if enabled('TEMP'):
            for name, minimum, maximum in (('CHG_OT', 800, 1100), ('CHG_UT', 200, 500),
                    ('DSG_OT', 850, 1200), ('DSG_UT', 0, 500)):
                trip, recover = value(name + '_X10'), value(name + '_RECOVER_X10')
                require(minimum <= trip <= maximum,
                        f'{name}_X10={trip}（{trip / 10 - 40:g}℃），允许 {minimum / 10 - 40:g}..{maximum / 10 - 40:g}℃')
                require(recover < trip if name.endswith('OT') else recover > trip,
                        f'{name}_RECOVER_X10 必须' + ('低于' if name.endswith('OT') else '高于') + '保护温度')
        return errors

    def registers(self):
        v = self.value
        if self.dvc:
            cov = max(501, min(v('COV_MV'), 4595)) - 500 if v('ENABLE_COV') and v('COV_MV') else 0
            cuv = min(v('CUV_MV'), 4095) if v('ENABLE_CUV') else 0
            result = {0x70: cov >> 4, 0x71: ((cov & 15) << 4) | floor_index(v('COV_DELAY_MS'), DVC_V_DELAY),
                      0x72: cuv >> 4, 0x73: ((cuv & 15) << 4) | floor_index(v('CUV_DELAY_MS'), DVC_V_DELAY)}
            for address, name in ((0x59, 'OCD1'), (0x5A, 'OCC1')):
                request = min(v(name + '_A10'), int(Fraction(637500) / self.shunt))
                q = Fraction(request) * self.shunt / 2500
                result[address] = max(1, min(255, int(q))) if v('ENABLE_' + name) and request else 0
            for address, name in ((0x5E, 'OCD2'), (0x5F, 'OCC2')):
                request = min(v(name + '_A10'), int(Fraction(2560000) / self.shunt))
                count = int(Fraction(request) * self.shunt / 40000)
                result[address] = 64 | max(0,min(63,count-1)) if v('ENABLE_' + name) and request else 0
            for address, name, step in ((0x5B, 'OCD1', 8), (0x5C, 'OCC1', 8), (0x60, 'OCD2', 4), (0x61, 'OCC2', 4)):
                result[address] = max(0, min(255, v(name + '_DELAY_MS') // step - 1))
            sc_a10 = int(Fraction(v('SC_THRESHOLD_MV') * 10000) / self.shunt)
            if sc_a10 > 65535: raise ValueError('短路电流超出固件 0.1A 参数表示范围')
            sc_code = int(Fraction(sc_a10) * self.shunt / 100000)
            result[0x62] = (64 | sc_code) if v('ENABLE_SC') and sc_a10 else 0
            result[0x63] = v('SC_DELAY_US') * 100 // 781 if result[0x62] else 0
            return result
        ov = min(1023, (v('COV_MV') + 2) // 5)
        uv = min(1023, (v('CUV_MV') + 2) // 5)
        result = {0x49: (ceiling(v('COV_DELAY_MS'), SH_OV_DELAY) << 4) | (ov >> 8), 0x4A: ov & 255,
                  0x4B: (ceiling(v('CUV_DELAY_MS'), SH_UV_DELAY) << 4) | (uv >> 8), 0x4C: uv & 255,
                  0x4D: (ceiling(v('OCD1_DELAY_MS'), SH_OV_DELAY) << 4) | self.current_code('OCD1_A10', 5000, 15),
                  0x4E: (max(1, min(16, ceil_fraction(Fraction(v('OCD2_DELAY_MS'), 25)))) - 1) << 4 | self.current_code('OCD2_A10', 10000, 15),
                  0x50: ceiling(v('OCC1_DELAY_MS'), SH_OV_DELAY) << 5 | self.current_code('OCC1_A10', 1375, 31)}
        base_a10 = Fraction(((result[0x4E] & 15) + 1) * 100000, 1) / self.shunt
        result[0x4F] = ((ceiling(v('SC_A10'), [base_a10 * m for m in (2, 3, 4, 6)]) << 4) |
                        ceiling(v('SC_DELAY_US'), SH_SC_DELAY)) if v('ENABLE_SC') else 0
        result[0x45] = (v('ENABLE_COV') | (v('ENABLE_CUV') << 1) |
                        ((v('ENABLE_OCD1') or v('ENABLE_OCD2')) << 2) | (v('ENABLE_SC') << 3) | (v('ENABLE_TEMP') * 0x30))
        # SCONF5 这里只拥有 OCC_EN，不能把其余位显示为已知源码值。
        result[0x44] = v('ENABLE_OCC1') << 4
        for address, name, low in ((0x51, 'CHG_OT_X10', False), (0x52, 'DSG_OT_X10', False),
                                   (0x53, 'CHG_UT_X10', True), (0x54, 'DSG_UT_X10', True)):
            result[address] = self.temp_code(v(name), low)
        return result

    def owns(self, address, mask):
        if self.dvc: return address in (0x59, 0x5A, 0x5B, 0x5C, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x70, 0x71, 0x72, 0x73)
        return (address == 0x44 and mask == 0x10 or address == 0x45 and mask < 0x40 or 0x49 <= address <= 0x54)

    def edit(self, address, mask, shift, code):
        """字段编码反解到唯一参数所有者；无法精确往返时拒绝静默换成别的编码。"""
        before = {name: row['edit'] for name, row in self.rows.items()}
        try:
            regs = self.registers()
            if regs.get(address) is None: raise ValueError('当前温度值不可编码，请先在参数页修正温度范围')
            target = (regs[address] & ~mask) | (code << shift)
            if self.dvc: self.edit_dvc(address, mask, code, target, regs)
            else: self.edit_sh(address, mask, code, target, regs)
            if (self.registers()[address] & mask) >> shift != code:
                raise ValueError('该原始编码不能被当前物理参数单位精确表示；请用物理参数或内部版临时调试，未更改源码参数')
        except Exception:
            for name, value in before.items(): self.rows[name]['edit'] = value
            raise

    def set_current(self, name, code, step, up):
        nominal = Fraction((code + 1) * step * 10, 1) / self.shunt
        value = nominal.numerator // nominal.denominator if up else ceil_fraction(nominal)
        self.set(name, value)

    def edit_sh(self, address, mask, code, target, regs):
        if address in (0x49, 0x4A, 0x4B, 0x4C):
            cov = address < 0x4B; high = 0x49 if cov else 0x4B; name = 'COV' if cov else 'CUV'
            if address == high and mask == 0x70:
                self.set(name + '_DELAY_MS', (SH_OV_DELAY if cov else SH_UV_DELAY)[code]); return
            logical = ((target & 3) << 8 | regs[high + 1]) if address == high else ((regs[high] & 3) << 8 | target)
            mv = logical * 5
            if not (3000 <= mv <= 4500 if cov else 1000 <= mv <= 3500):
                raise ValueError('原厂保证范围：OV 3000..4500mV，UV 1000..3500mV；跨字节字段需同时考虑另一字节')
            self.set(name + '_MV', mv)
        elif address in (0x4D, 0x4E, 0x50):
            name, step = {0x4D: ('OCD1', 5000), 0x4E: ('OCD2', 10000), 0x50: ('OCC1', 1375)}[address]
            if mask in (0x70, 0xF0, 0xE0): self.set(name + '_DELAY_MS', (code + 1) * 25 if address == 0x4E else SH_OV_DELAY[code])
            else: self.set_current(name + '_A10', code, step, True)
        elif address == 0x4F:
            if mask == 15: self.set('SC_DELAY_US', SH_SC_DELAY[code])
            else: self.set('SC_A10', int(Fraction(((regs[0x4E] & 15) + 1) * 100000 * (2, 3, 4, 6)[code], 1) / self.shunt))
        elif 0x51 <= address <= 0x54:
            name = {0x51: 'CHG_OT_X10', 0x52: 'DSG_OT_X10', 0x53: 'CHG_UT_X10', 0x54: 'DSG_UT_X10'}[address]
            allowed = {0x51: range(800, 1101), 0x52: range(850, 1201),
                       0x53: range(200, 501), 0x54: range(0, 501)}[address]
            matches = [t for t in allowed if self.temp_code(t, address >= 0x53) == code]
            if not matches: raise ValueError('该编码在本板 10K NTC 表和固件允许温度范围内没有对应值；可查手册或做内部版临时调试')
            self.set(name, min(matches, key=lambda t: abs(t - self.value(name))))
        elif address == 0x44: self.set('ENABLE_OCC1', code)
        elif address == 0x45:
            names = {1: ('COV',), 2: ('CUV',), 4: ('OCD1', 'OCD2'), 8: ('SC',), 16: ('TEMP',), 32: ('TEMP',)}[mask]
            for name in names: self.set('ENABLE_' + name, code)

    def edit_dvc(self, address, mask, code, target, regs):
        if address in (0x70, 0x71, 0x72, 0x73):
            cov = address < 0x72; high = 0x70 if cov else 0x72; name = 'COV' if cov else 'CUV'
            if mask == 15: self.set(name + '_DELAY_MS', DVC_V_DELAY[code]); return
            logical = (target << 4 | regs[high + 1] >> 4) if address == high else (regs[high] << 4 | target >> 4)
            self.set(name + '_MV', logical + 500 if cov and logical else logical)
            self.set('ENABLE_' + name, int(logical != 0))
        elif address in (0x59, 0x5A):
            name = 'OCD1' if address == 0x59 else 'OCC1'
            self.set(name + '_A10', ceil_fraction(Fraction(code * 2500, 1) / self.shunt))
            self.set('ENABLE_' + name, int(code != 0))
        elif address in (0x5E, 0x5F):
            name = 'OCD2' if address == 0x5E else 'OCC2'
            if mask == 64: self.set('ENABLE_' + name, code)
            else: self.set_current(name + '_A10', code, 4000, False)
        elif address in (0x5B, 0x5C, 0x60, 0x61):
            name = {0x5B: 'OCD1', 0x5C: 'OCC1', 0x60: 'OCD2', 0x61: 'OCC2'}[address]
            self.set(name + '_DELAY_MS', (code + 1) * (8 if address < 0x60 else 4))
        elif address == 0x62:
            if mask == 64: self.set('ENABLE_SC', code)
            else:
                if not code: raise ValueError('SCDT=0 不得用于启用 SCD；关闭请编辑 SCDE')
                self.set('SC_THRESHOLD_MV', code * 10)
        elif address == 0x63: self.set('SC_DELAY_US', ceil_fraction(Fraction(code * 781, 100)))

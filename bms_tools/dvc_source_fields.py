"""DVC1124-2 RM V1.2 第 18～20、23～24 页的保护字段与 D008 源码参数换算。"""
from bisect import bisect_right


VOLTAGE_DELAYS = (200, 300, 400, 500, 600, 700, 800, 900, 1000,
                  2000, 3000, 4000, 5000, 6000, 7000, 8000)
CURRENT_THRESHOLDS = {
    0x59: ("OCD1", 2500, 255), 0x5A: ("OCC1", 2500, 255),
    0x5E: ("OCD2", 40000, 64), 0x5F: ("OCC2", 40000, 64),
}
CURRENT_DELAYS = {0x5B: ("OCD1", 8), 0x5C: ("OCC1", 8),
                  0x60: ("OCD2", 4), 0x61: ("OCC2", 4)}
FIELD_OWNERS = {
    **{(addr, 0xFF if count == 255 else 0x3F): name + "_A10"
       for addr, (name, _, count) in CURRENT_THRESHOLDS.items()},
    **{(addr, 0xFF): name + "_DELAY_MS" for addr, (name, _) in CURRENT_DELAYS.items()},
    (0x5E, 0x40): "ENABLE_OCD2", (0x5F, 0x40): "ENABLE_OCC2",
    (0x62, 0x3F): "SC_THRESHOLD_MV", (0x62, 0x40): "ENABLE_SC",
    (0x63, 0xFF): "SC_DELAY_US",
    (0x70, 0xFF): "COV_MV", (0x71, 0xF0): "COV_MV", (0x71, 0x0F): "COV_DELAY_MS",
    (0x72, 0xFF): "CUV_MV", (0x73, 0xF0): "CUV_MV", (0x73, 0x0F): "CUV_DELAY_MS",
}


def register_values(value, shunt: int) -> dict[int, int]:
    """预览保护默认值经固件量化后的字段值。"""
    regs = {}
    for high, name, offset in ((0x70, "COV", 500), (0x72, "CUV", 0)):
        mv = value(name + "_MV")
        code = min(4095, max(1, mv - offset)) if value("ENABLE_" + name) and mv else 0
        delay = max(0, bisect_right(VOLTAGE_DELAYS, value(name + "_DELAY_MS")) - 1)
        regs[high] = code >> 4
        regs[high + 1] = ((code & 15) << 4) | delay
    for addr, (name, step, count) in CURRENT_THRESHOLDS.items():
        current = min(value(name + "_A10"), step * count // shunt)
        quantized = min(count, max(1, current * shunt // step))
        regs[addr] = ((quantized if count == 255 else 0x40 | (quantized - 1))
                      if value("ENABLE_" + name) and current else 0)
    for addr, (name, step) in CURRENT_DELAYS.items():
        regs[addr] = min(255, max(0, value(name + "_DELAY_MS") // step - 1))
    sc_current = value("SC_THRESHOLD_MV") * 10000 // shunt
    sc_code = sc_current * shunt // 100000
    sc_enabled = value("ENABLE_SC") and sc_current
    sc_delay = value("SC_DELAY_US") * 100 // 781
    if sc_enabled and (sc_current > 65535 or not 1 <= sc_code <= 63 or sc_delay > 255):
        raise ValueError("短路阈值或延时超出当前分流器可表示的保护档位。")
    regs[0x62] = (0x40 | sc_code) if sc_enabled else 0
    regs[0x63] = sc_delay if sc_enabled else 0
    return regs


def edit_field(addr: int, mask: int, code: int, value, shunt: int) -> dict[str, int]:
    """将编辑过的保护位还原到现有宏，并核对正向编码。"""
    name = FIELD_OWNERS[addr, mask]
    shift = (mask & -mask).bit_length() - 1
    if not 0 <= code <= mask >> shift:
        raise ValueError("编码超出位宽。")
    regs = register_values(value, shunt)
    target = (regs[addr] & ~mask) | (code << shift)
    if target == regs[addr]:
        return {}
    updated = {}
    if addr in (0x70, 0x71, 0x72, 0x73):
        if mask == 15:
            updated[name] = VOLTAGE_DELAYS[code]
        else:
            high = addr & ~1
            logical = (target << 4) | (regs[high + 1] >> 4) if addr == high else (
                (regs[high] << 4) | (target >> 4))
            updated[name] = logical + (500 if high == 0x70 and logical else 0)
            updated["ENABLE_" + name[:3]] = int(logical != 0)
    elif addr in CURRENT_THRESHOLDS and mask != 0x40:
        prefix, step, count = CURRENT_THRESHOLDS[addr]
        level = code if count == 255 else code + 1
        updated[name] = (level * step + shunt - 1) // shunt
        if count == 255:
            updated["ENABLE_" + prefix] = int(code != 0)
    elif addr in CURRENT_DELAYS:
        updated[name] = (code + 1) * CURRENT_DELAYS[addr][1]
    elif addr == 0x62 and mask == 0x3F:
        if code == 0:
            raise ValueError("关闭短路保护请使用 SCDE。")
        updated[name] = code * 10
    elif addr == 0x63:
        updated[name] = (code * 781 + 99) // 100
    else:
        updated[name] = code
    projected = register_values(lambda key: updated[key] if key in updated else value(key), shunt)
    if projected[addr] & mask != target & mask:
        raise ValueError("该编码不能用当前分流器和源码参数精确表示，请编辑物理参数。")
    return {"D008_AFE_" + key: raw for key, raw in updated.items()}

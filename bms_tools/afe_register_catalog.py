"""从版本化原厂资料索引生成桌面目录和固件访问掩码；不修改原资料。"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = Path("references/afe/AFE_完整开发参考库_20261005")


def catalog(root: Path = ROOT) -> dict:
    families = []
    source_hashes = {}
    for chip in ("dvc1124_2", "sh36735xx"):
        relative = REFERENCE / "chips" / chip / "registers.json"
        raw = (root / relative).read_bytes()
        source_hashes[relative.as_posix()] = hashlib.sha256(raw).hexdigest()
        data = json.loads(raw.decode("utf-8-sig"))
        revisions = [None] if chip == "dvc1124_2" else ["CV1.0A", "CV0.2C"]
        for revision in revisions:
            registers = []
            for original in data["registers"]:
                if revision and original["revision"] != revision:
                    continue
                dvc = chip == "dvc1124_2"
                address = original["address_decimal"] if dvc else original["address"]
                fields = []
                writable = 0
                read_clear = False
                for field in original["bitfields"]:
                    mask = int(field["mask"], 16)
                    access = field["access"]
                    reserved = field.get("reserved_or_undocumented", field.get("reserved", False))
                    if access in ("RC", "read_clear") or field.get("read_side_effect"):
                        read_clear = True
                    semantics = [data.get("field_semantics", {}).get(key, {})
                                 for key in field.get("semantic_keys", [])]
                    description = field.get("description_zh") or "；".join(
                        item.get("description_zh", "") for item in semantics)
                    encoding = field.get("encoding") or [item.get("encodings") for item in semantics]
                    if access in ("RW", "R/W") and not reserved:
                        writable |= mask
                    fields.append(dict(name=field["name"], mask=mask,
                                       shift=(mask & -mask).bit_length() - 1,
                                       access=access, reserved=reserved,
                                       description=description, encoding=encoding,
                                       logical_width=field.get("logical_field_width_bits"),
                                       encoding_input=field.get("encoding_input"),
                                       combine_code=field.get("combine_code"),
                                       read_side_effect=field.get("read_side_effect"),
                                       write_side_effect=field.get("write_side_effect"),
                                       write_1_behavior=field.get("write_1_behavior")))
                    if dvc and field['name'] in ('COVT','CUVT'):
                        # RM p23..24：高字节含 bit11..4，低字节高半字节含 bit3..0。
                        high = 0x70 if field['name']=='COVT' else 0x72
                        fields[-1].update(logical_width=12,
                            encoding_input='公式中的编码是完整 12 位阈值，当前行仅为其中一部分；低字节低 4 位另属延时。',
                            combine_code=f'(reg_0x{high:02X} << 4) | (reg_0x{high+1:02X} >> 4)')
                # 读清除和命令不能走通用 RMW。MODE/FET/均衡/GP 路由须由业务入口拥有。
                if dvc:
                    read_clear = read_clear or address in (0x01, 0x76)
                    if address in (0x00, 0x01, 0x51, 0x55, 0x67, 0x68, 0x69,
                                   0x74, 0x75, 0x76, 0x90):
                        writable = 0
                    # RM p22 COW 是自清触发；p26 V3P3M 是供电超时策略，留给后端。
                    if address == 0x6D: writable &= ~0x04
                    if address == 0x77: writable &= ~0x20
                    source_mask = writable if address in (0x52, 0x56, 0x65, 0x66,
                                                         0x78, 0x79) else 0
                    # CM 由装配串数拥有；CMM/CVS/VADC 和 IWT/供电由后端固定策略拥有。
                    if address == 0x6D: source_mask = writable & 0x38
                    sources = [original["source"]]
                else:
                    # V1.0A p40-41：FLAG3.OWD_FLG 也是读清除，不能在普通全读中消耗。
                    read_clear = read_clear or address in (0x59, 0x5A)
                    if address in (0x40, 0x55, 0x56, 0x57, 0x58, 0x59):
                        writable = 0
                    if address == 0x41:
                        writable &= 0x50  # PD_EN/PUMP_EN；不写 LTCLR/PD_CTL/FET 命令。
                    if address == 0x42:
                        writable &= 0x7E  # 排除 OWD_TRG 和保留位。
                    source_mask = writable if address in (0x46, 0x47, 0x48) else (0x50 if address == 0x41 else 0x40 if address == 0x42 else 0xE0 if address == 0x43 else 0)
                    sources = original["sources"]
                registers.append(dict(address=address, name=original["name"],
                    reset=original.get("reset_hex", original.get("reset")),
                    read_clear=read_clear, readable=not (dvc and address == 0x90),
                    write_mask=writable, source_mask=source_mask, fields=fields,
                    sources=sources, notes=original.get("notes", [])))
            families.append(dict(id=chip if not revision else chip + "_" + revision,
                model="DVC1124-2" if chip == "dvc1124_2" else "SH36735xx",
                revision=revision or "RM V1.2 / DS V1.1", registers=registers,
                issues=data.get("known_issues", [])))
    source_dir = root / REFERENCE / "sources"
    for path in sorted(source_dir.glob("*.pdf")):
        if path.name.startswith(("DVC1124-2", "SH36735XX")):
            source_hashes[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    result = dict(schema=1, sources=source_hashes, families=families)
    digest = hashlib.sha256(json.dumps(result, ensure_ascii=False, sort_keys=True).encode("utf-8")).hexdigest()
    result["catalog_id"] = digest[:8]
    return result


def generate(root: Path = ROOT, host: Path | None = None) -> None:
    data = catalog(root)
    encoded = (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    (root / "bms_tools/afe-register-catalog.json").write_bytes(encoded)
    lines = ["/* 由 bms_tools/afe_register_catalog.py 生成；原页/哈希见桌面目录。 */",
             "#ifndef BMS_AFE_REGISTER_POLICY_H_", "#define BMS_AFE_REGISTER_POLICY_H_",
             f"#define BMS_AFE_REGISTER_CATALOG_ID 0x{data['catalog_id']}UL",
             "#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124"]
    for index in (0, 1):
        if index:
            lines.append("#else")
        registers = data["families"][index]["registers"]
        lines.append("#if BMS_AFE_REGISTER_DEBUG_ENABLE")
        lines.append("static const uint8_t s_register_write_masks[] = {")
        for register in registers:
            lines.append(f"    0x{register['write_mask']:02X}u, /* 0x{register['address']:02X} */")
        lines.append("};")
        lines.append("#endif")
        lines.append("#if BMS_AFE_REGISTER_PRESET_COUNT")
        lines.append("static const uint8_t s_register_source_masks[] = {")
        for register in registers:
            lines.append(f"    0x{register['source_mask']:02X}u, /* 0x{register['address']:02X} */")
        lines.append("};")
        lines.append("#endif")
    lines.extend(["#endif", "#endif", ""])
    (root / "bms/core/bms_afe_register_policy.h").write_text("\n".join(lines), encoding="utf-8")
    if host is not None:
        (host / "bms-tool-windows/BmsFactoryTest.Windows/afe-register-catalog.json").write_bytes(encoded)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-root", type=Path)
    args = parser.parse_args()
    generate(host=args.host_root)

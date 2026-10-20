"""源码参数与原厂 AFE 位查看器；仅编辑源码，不编译、不连接设备。"""
from __future__ import annotations

import ast
import copy
import difflib
import json
import os
import re
from datetime import datetime, timezone
from fractions import Fraction
from decimal import Decimal, localcontext
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

import build_options
from afe_register_catalog import REFERENCE
from afe_source_fields import SourceProtectionFields, field_help

ROOT = Path(__file__).resolve().parents[1]
CATALOG = json.loads((ROOT / "bms_tools/afe-register-catalog.json").read_text(encoding="utf-8"))
NUMBER = r"(?:0x[0-9a-fA-F]+|\d+)[uUlL]*"
DEFINE = re.compile(r"(?m)^#define\s+(\w+)(\(c\))?[ \t]+([^\r\n]+)")
TERNARY = re.compile(r"\(\(c\) == BMS_SOC_CHEMISTRY_LFP \? (" + NUMBER + r") : (" + NUMBER + r")\)")


def integer(expression: str) -> int:
    """只接受整数常量及有限四则运算；不执行 C/Python 任意表达式。"""
    expression = re.sub(r"\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b", r"\1", expression.strip())
    tree = ast.parse(expression, mode="eval")

    def visit(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            value = visit(node.operand)
            return value if isinstance(node.op, ast.UAdd) else -value
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub, ast.Mult, ast.Div, ast.FloorDiv)):
            a, b = visit(node.left), visit(node.right)
            if isinstance(node.op, ast.Add): return a + b
            if isinstance(node.op, ast.Sub): return a - b
            if isinstance(node.op, ast.Mult): return a * b
            if not b or a % b: raise ValueError("参数表达式必须得到精确整数")
            return a // b
        raise ValueError("该宏由其它字段计算，不能作为独立参数编辑")

    return visit(tree.body)


def display_scale(name: str):
    """仅在界面边界换算；Configuration、JSON 和寄存器编码仍保存源码整数。"""
    name = name.removesuffix("_FOR").removesuffix("_DEFAULT")
    if name.endswith("_A10"): return Fraction(1, 10), 0
    if name.endswith("_MA"): return Fraction(1, 1000), 0
    if name.endswith("_X10"): return Fraction(1, 10), -40
    if name.endswith("_0P1AH"): return Fraction(1, 10), 0
    if name.endswith("_10MS"): return Fraction(10), 0
    if name.startswith("BMS_SLEEP_") and name.endswith(("_SECONDS", "_S")):
        return Fraction(1, 60), 0
    return Fraction(1), 0


def display_value(name: str, raw) -> str:
    scale, offset = display_scale(name)
    value = integer(str(raw)) * scale + offset
    denominator = value.denominator
    for factor in (2, 5):
        while denominator % factor == 0: denominator //= factor
    if denominator != 1: return str(value)  # 非整分钟保留精确分数，避免读写时无意取整。
    with localcontext() as context:
        context.prec = 40
        text = format(Decimal(value.numerator) / Decimal(value.denominator), "f")
    return text.rstrip("0").rstrip(".") if "." in text else text


def source_value(name: str, text: str) -> str:
    scale, offset = display_scale(name)
    value = (Fraction(text.strip()) - offset) / scale
    if value.denominator != 1 or value < 0:
        raise ValueError("数值不能精确表示为源码非负整数，请按该参数精度输入；不会自动四舍五入。")
    return str(value.numerator)


def unit(name: str) -> str:
    name = name.removesuffix("_FOR").removesuffix("_DEFAULT")
    if name.startswith("BMS_SLEEP_") and name.endswith(("_SECONDS", "_S")): return "分钟"
    for suffix, text in (("_10MS", "ms"), ("_A10", "A"), ("_X10", "℃"),
                         ("_UOHM", "µΩ"), ("_MV", "mV"), ("_MS", "ms"), ("_US", "µs"),
                         ("_MA", "A"), ("_0P1AH", "Ah"), ("_PERCENT", "%")):
        if name.endswith(suffix): return text
    return "整数/编码"


def label(name: str) -> str:
    text = name
    replacements = (("SH3673510_HW_DEFAULT_", "AFE "), ("D008_AFE_", "AFE "),
        ("D008_SW_", "软件 "), ("BMS_SW_", "软件 "), ("CELL_OVP", "单体过压"),
        ("CELL_UVP", "单体欠压"), ("PACK_OVP", "总压过压(单串)"), ("PACK_UVP", "总压欠压(单串)"),
        ("DISCHARGE_OCP", "放电过流"), ("CHARGE_OCP", "充电过流"),
        ("DISCHARGE_OTP", "放电高温"), ("CHARGE_OTP", "充电高温"),
        ("DISCHARGE_UTP", "放电低温"), ("CHARGE_UTP", "充电低温"),
        ("FIRST", "一级"), ("SECOND", "二级"), ("THIRD", "三级"),
        ("RECOVER", "恢复"), ("FILTER", "滤波"), ("COV", "单体过压"), ("CUV", "单体欠压"),
        ("OCD1", "放电过流1"), ("OCD2", "放电过流2"), ("OCC1", "充电过流1"),
        ("OCC2", "充电过流2"), ("SC_", "短路_"))
    for a, b in replacements: text = text.replace(a, b)
    return text.replace("_FOR", "").replace("_", " ")


class Configuration:
    def __init__(self, product: str, chemistry: str):
        self.root = ROOT
        self.product, self.chemistry = product, chemistry
        self.folder = ROOT / "bms/products" / product
        self.settings = build_options.read_source(ROOT, product, for_edit=True)
        self.baseline_settings = dict(self.settings)
        self.paths = [self.folder / "bms_product.h", self.folder / f"{product}_protection_config.h",
                      self.folder / "afe_register_config.h"]
        if product == "d008": self.paths.append(self.folder / "dvc1124_product_defaults.h")
        if product == "d008": self.paths.append(self.folder / "d008_product_profile.h")
        self.originals = {path: path.read_bytes() for path in self.paths}
        self.rows = []
        for path, raw in self.originals.items():
            text = raw.decode("utf-8-sig")
            for match in DEFINE.finditer(text):
                name, argument, expression = match.groups()
                if not name.startswith(("D008_SW_", "D008_AFE_", "BMS_SW_", "SH3673510_HW_DEFAULT_",
                                        "BMS_SLEEP_", "BMS_BALANCE_", "BMS_HEATER_", "BMS_BATTERY_", "BMS_SOC_")):
                    continue
                expression = expression.split("/*", 1)[0].strip()
                try:
                    ternary = TERNARY.fullmatch(expression) if argument else None
                    value = integer(ternary.group(1 if chemistry == "lfp" else 2)) if ternary else integer(expression)
                except (ValueError, SyntaxError, ZeroDivisionError):
                    continue
                branches = {"lfp": integer(ternary.group(1)), "nmc": integer(ternary.group(2))} if ternary else {}
                self.rows.append(dict(path=path, name=name, argument=argument or "", value=value,
                    ternary=bool(ternary), edit=str(value), scope=product.upper(), branches=branches, edits=dict(branches)))
        self.family = CATALOG["families"][0 if product == "d008" else 1]
        board = self.originals[self.folder / "bms_product.h"].decode("utf-8-sig")
        if product == "d008":
            fixed = self.originals[self.folder / "dvc1124_product_defaults.h"].decode("utf-8-sig")
            self.element = build_options._number(fixed, "DVC1124_DEFAULT_SHUNT_UOHM")
            self.parallel = 1
        else:
            self.element = build_options._number(board, "SH3673510_BOARD_SHUNT_ELEMENT_UOHM") if "#define SH3673510_BOARD_SHUNT_ELEMENT_UOHM" in board else build_options._number(board, "SH3673510_BOARD_SHUNT_UOHM")
            self.parallel = build_options._number(board, "SH3673510_BOARD_SHUNT_PARALLEL_COUNT") if "#define SH3673510_BOARD_SHUNT_PARALLEL_COUNT" in board else 1
        self.presets = {}
        source = self.originals[self.folder / "afe_register_config.h"].decode("utf-8-sig")
        self.debug = bool(build_options._number(source, "BMS_AFE_REGISTER_DEBUG_ENABLE"))
        def profile_text(name):
            found = re.search(r"(?m)^#define\s+" + name + r"\s+([^\r\n]+)", source)
            return json.loads(found[1]) if found else ""
        self.customer_name = profile_text("BMS_SOURCE_CUSTOMER_NAME")
        self.customer_revision = profile_text("BMS_SOURCE_CUSTOMER_REVISION")
        for address, mask, value in re.findall(r"X\((0x[0-9A-Fa-f]+)u?,\s*(0x[0-9A-Fa-f]+)u?,\s*(0x[0-9A-Fa-f]+)u?\)", source):
            self.presets[int(address, 16)] = (int(mask, 16), int(value, 16))
        self.baseline_extra=(self.element,self.parallel,self.debug,dict(self.presets),self.customer_name,self.customer_revision)

    def sync_branches(self):
        for row in self.rows:
            if row['ternary']: row['edits'][self.chemistry] = integer(row['edit'])

    def change_chemistry(self, chemistry):
        self.sync_branches()
        self.chemistry = chemistry
        for row in self.rows:
            if row['ternary']:
                row['value'] = row['branches'][chemistry]
                row['edit'] = str(row['edits'][chemistry])

    def parameters_changed(self):
        self.sync_branches()
        return any(row['edits'] != row['branches'] if row['ternary'] else integer(row['edit']) != row['value'] for row in self.rows)

    def profile(self, element, parallel, debug, name, revision):
        self.sync_branches()
        if not name.strip() or not revision.strip(): raise ValueError('请填写客户/方案名称与版本；另存新版本可避免覆盖旧客户方案')
        self.customer_name, self.customer_revision = name.strip(), revision.strip()
        self.replacements(element, parallel, debug)  # 复用源码保存的范围和预设校验。
        return dict(schema=1, product=self.product, afe_model=self.settings['afe_model'],
                    name=self.customer_name, revision=self.customer_revision, view_chemistry=self.chemistry,
                    created_utc=datetime.now(timezone.utc).isoformat(), catalog_id=CATALOG['catalog_id'],
                    assembly={key: self.settings[key] for key in ('afe_model','cell_count','chemistry','capacity_0p1ah')},
                    parameters={r['name']: dict(r['edits']) if r['ternary'] else integer(r['edit']) for r in self.rows},
                    shunt=dict(element_uohm=element, parallel_count=parallel), debug_enabled=bool(debug),
                    presets=[dict(address=a, mask=m, value=v) for a,(m,v) in sorted(self.presets.items())])

    def load_profile(self, data):
        """只载入窗口；完全校验后才更改模型，不隐式写源码。"""
        if not isinstance(data, dict) or type(data.get('schema')) is not int or data['schema'] != 1: raise ValueError('不支持的客户方案格式')
        expected_keys={'schema','product','afe_model','name','revision','view_chemistry','created_utc','catalog_id','assembly','parameters','shunt','debug_enabled','presets'}
        if set(data) != expected_keys: raise ValueError('客户方案缺少字段或包含未知字段，请核对版本')
        if data['product'] != self.product or data['afe_model'] != self.settings['afe_model']: raise ValueError('方案的产品或 AFE 型号与当前选择不匹配；禁止套用到另一块板')
        if data['catalog_id'] != CATALOG['catalog_id']: raise ValueError('客户方案的 AFE 目录版本不一致，请核对原厂字段后重新保存')
        for key in ('name','revision'):
            if not isinstance(data[key],str) or not 1 <= len(data[key].strip()) <= 80 or any(ord(ch)<32 for ch in data[key]): raise ValueError('方案名称/版本须为 1..80 个普通字符')
        if data['view_chemistry'] not in ('lfp','nmc') or type(data['debug_enabled']) is not bool: raise ValueError('方案类型或调试开关无效')
        if not isinstance(data['parameters'],dict) or set(data['parameters']) != {r['name'] for r in self.rows}: raise ValueError('参数集合与当前产品源码不一致，不能用旧方案静默覆盖新参数')
        for row in self.rows:
            values=data['parameters'][row['name']]
            values=list(values.values()) if row['ternary'] and isinstance(values,dict) and set(values)=={'lfp','nmc'} else [values] if not row['ternary'] else []
            if not values or any(type(v) is not int or not 0 <= v <= (86400*365 if row['name'].endswith('_SECONDS') else 65535) for v in values): raise ValueError('方案参数越界：'+row['name'])
            if 'ENABLE_' in row['name'] and any(v not in (0,1) for v in values): raise ValueError('开关仅允许 0/1：'+row['name'])
        assembly=data['assembly']
        if not isinstance(assembly,dict) or set(assembly)!={'afe_model','cell_count','chemistry','capacity_0p1ah'} or assembly['afe_model']!=self.settings['afe_model']: raise ValueError('装配字段无效，方案不允许改变 AFE 硬件型号')
        if any(type(assembly[key]) is not int for key in ('cell_count','capacity_0p1ah')): raise ValueError('串数/容量必须为整数')
        normalized=build_options.normalize({**self.settings,**assembly},self.product,'custom' if self.product=='d008' else None)
        shunt=data['shunt']
        if not isinstance(shunt,dict) or set(shunt)!={'element_uohm','parallel_count'} or any(type(v) is not int for v in shunt.values()): raise ValueError('分流器比例格式错误')
        element,parallel=shunt['element_uohm'],shunt['parallel_count']
        if not 1<=element<=60000 or not 1<=parallel<=16 or element*2<parallel or (self.product=='d008' and element%parallel): raise ValueError('分流器比例不可表示')
        policies={r['address']:r['source_mask'] for r in self.family['registers']};presets={}
        if not isinstance(data['presets'],list) or len(data['presets'])>145: raise ValueError('固定字段预设数量无效')
        for entry in data['presets']:
            if not isinstance(entry,dict) or set(entry)!={'address','mask','value'} or any(type(v) is not int or not 0<=v<=255 for v in entry.values()): raise ValueError('预设格式无效')
            a,m,v=entry['address'],entry['mask'],entry['value']
            if a in presets or not m or m & ~policies.get(a,0) or v & ~m: raise ValueError('预设含重复、禁止或保留字段')
            if self.product=='d008' and a==0x52 and m&31==31 and v&31==31: raise ValueError('DPC=31 为 N/A')
            presets[a]=(m,v)
        self.settings=normalized;self.change_chemistry(data['view_chemistry'])
        for row in self.rows:
            value=data['parameters'][row['name']]
            if row['ternary']: row['edits']=dict(value);value=value[self.chemistry]
            row['edit']=str(value)
        self.element,self.parallel,self.debug=element,parallel,data['debug_enabled'];self.presets=presets
        self.customer_name,self.customer_revision=data['name'].strip(),data['revision'].strip()

    def replacements(self, element: int, parallel: int, debug: bool) -> dict:
        self.sync_branches()
        if self.product!='d008':
            switches={r['name']:integer(r['edit']) for r in self.rows if 'ENABLE_OCD' in r['name']}
            if len(set(switches.values()))>1: raise ValueError('SH OCD1/OCD2 共用硬件许可，两个开关必须相同')
        for value in (self.customer_name,self.customer_revision):
            if not isinstance(value,str) or len(value)>80 or any(ord(ch)<32 for ch in value): raise ValueError('客户方案名称/版本最多 80 个普通字符')
        if not 1 <= element <= 60000 or not 1 <= parallel <= 16:
            raise ValueError("单只/分子为 1..60000 µΩ，并联只数/分母为 1..16")
        edited = {p: b.decode("utf-8-sig") for p, b in self.originals.items()}
        for row in self.rows:
            value = integer(row["edit"])
            maximum = 86400 * 365 if row["name"].endswith("_SECONDS") else 65535
            values=list(row['edits'].values()) if row['ternary'] else [value]
            if any(not 0 <= v <= maximum for v in values):
                raise ValueError(f"{row['name']} 超出允许范围 0..{maximum}")
            if 'ENABLE_' in row['name'] and any(v not in (0,1) for v in values):
                raise ValueError(row['name']+' 开关只允许 0/1')
            unchanged = row['edits'] == row['branches'] if row['ternary'] else value == row['value']
            if unchanged: continue
            pattern = re.compile(r"(?m)^(#define[ \t]+" + re.escape(row["name"] + row["argument"]) + r"[ \t]+)([^\r\n]+)")
            def replace(match):
                body = match[2]
                if row["ternary"]:
                    found = TERNARY.search(body)
                    if not found: raise ValueError("参数表达式已变化")
                    for number, chemistry in ((2,'nmc'),(1,'lfp')):
                        if row['edits'][chemistry] == row['branches'][chemistry]: continue
                        start,end=found.span(number)
                        body=body[:start]+str(row['edits'][chemistry])+"u"+body[end:]
                else:
                    comment = " /*" + body.split("/*", 1)[1] if "/*" in body else ""
                    body = str(value) + "u" + comment
                return match[1] + body
            edited[row["path"]], count = pattern.subn(replace, edited[row["path"]])
            if count != 1: raise ValueError("参数定义不唯一：" + row["name"])
        board_path = self.folder / "bms_product.h"
        protection_path=self.folder/f'{self.product}_protection_config.h'
        edited[protection_path]=build_options._replace(edited[protection_path],'BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH',str(self.settings['capacity_0p1ah'])+'u')
        if self.product!='d008':
            edited[board_path]=build_options._replace(edited[board_path],'SH3673510_BOARD_CELL_COUNT',str(self.settings['cell_count'])+'u',r'\d+[uU]?')
            edited[board_path]=build_options._replace(edited[board_path],'BMS_PRODUCT_CHEMISTRY','BMS_SOC_CHEMISTRY_'+self.settings['chemistry'].upper())
        else:
            path=self.folder/'d008_product_profile.h'
            profile=next((name for name,pair in build_options.PROFILES.items() if pair==(self.settings['cell_count'],self.settings['chemistry'])),'custom')
            edited[path]=build_options._replace(edited[path],'D008_PRODUCT_PROFILE','D008_PRODUCT_PROFILE_'+profile.upper().replace('-','_'))
            edited[path]=build_options._replace(edited[path],'D008_CUSTOM_CELL_COUNT',str(self.settings['cell_count'])+'u')
            edited[path]=build_options._replace(edited[path],'D008_CUSTOM_CHEMISTRY','BMS_SOC_CHEMISTRY_'+self.settings['chemistry'].upper())
        if self.product == "d008":
            if element % parallel: raise ValueError("DVC 当前接口使用整数 µΩ；该比例不可精确表达，不能静默取整")
            path = self.folder / "dvc1124_product_defaults.h"
            edited[path] = build_options._replace(edited[path], "DVC1124_DEFAULT_SHUNT_UOHM", str(element // parallel) + "u")
        else:
            board = edited[board_path]
            for name, value in (("SH3673510_BOARD_SHUNT_ELEMENT_UOHM", element),
                                ("SH3673510_BOARD_SHUNT_PARALLEL_COUNT", parallel)):
                if re.search(r"(?m)^#define\s+" + name + r"\s", board):
                    board = build_options._replace(board, name, str(value) + "u")
                else:
                    board += f"\n#define {name} {value}u\n"
            rounded = (element + parallel // 2) // parallel
            if not rounded: raise ValueError("旧协议整数微欧显示不能为 0")
            edited[board_path] = build_options._replace(board, "SH3673510_BOARD_SHUNT_UOHM", str(rounded) + "u")
        path = self.folder / "afe_register_config.h"
        config = "/* 源码配置窗口保存；保护与动态命令不进入固定字段预设。 */\n#pragma once\n#ifndef BMS_AFE_REGISTER_DEBUG_ENABLE\n"
        config += f"#define BMS_AFE_REGISTER_DEBUG_ENABLE {int(debug)}u\n#endif\n#define BMS_AFE_REGISTER_PRESET_COUNT {len(self.presets)}u\n#define BMS_AFE_REGISTER_PRESETS(X)"
        policies = {r["address"]: r["source_mask"] for r in self.family["registers"]}
        for address, (mask, value) in sorted(self.presets.items()):
            if not mask or mask & ~policies.get(address, 0) or value & ~mask:
                raise ValueError(f"0x{address:02X} 不是可持久固定字段；保护字段请在参数表编辑")
            if self.product=='d008' and address==0x52 and mask&31==31 and value&31==31:
                raise ValueError('DVC DPC=31 在 RM p16 标为 N/A，不可写入源码预设')
            config += f" \\\n    X(0x{address:02X}u, 0x{mask:02X}u, 0x{value:02X}u)"
        edited[path] = config + "\n"
        if self.customer_name or self.customer_revision:
            edited[path]+= '#define BMS_SOURCE_CUSTOMER_NAME '+json.dumps(self.customer_name,ensure_ascii=False)+'\n'
            edited[path]+= '#define BMS_SOURCE_CUSTOMER_REVISION '+json.dumps(self.customer_revision,ensure_ascii=False)+'\n'
        return {p: text for p, text in edited.items() if text != self.originals[p].decode("utf-8-sig")}

    def update_groups_for_changes(self, edited: dict):
        required = set()
        if any(self.settings[key] != self.baseline_settings[key]
               for key in ('afe_model', 'cell_count', 'chemistry')):
            required.update(build_options.CONFIG_UPDATE_GROUPS)
        if self.settings['capacity_0p1ah'] != self.baseline_settings['capacity_0p1ah']:
            required.add('business')
        for row in self.rows:
            changed = row['edits'] != row['branches'] if row['ternary'] else integer(row['edit']) != row['value']
            if not changed: continue
            name = row['name']
            if name.startswith(('D008_SW_', 'BMS_SW_')): required.add('sw')
            elif name.startswith(('D008_AFE_', 'SH3673510_HW_DEFAULT_')): required.add('afe')
            elif name.startswith('BMS_SOC_'): required.add('soc')
            else: required.add('business')
        shunt_path = self.folder / ('dvc1124_product_defaults.h' if self.product == 'd008' else 'bms_product.h')
        before_shunt = self.originals[shunt_path].decode('utf-8-sig')
        after_shunt = edited.get(shunt_path, before_shunt)
        shunt_macros = ('DVC1124_DEFAULT_SHUNT_UOHM',) if self.product == 'd008' else (
            'SH3673510_BOARD_SHUNT_ELEMENT_UOHM', 'SH3673510_BOARD_SHUNT_PARALLEL_COUNT')
        if any(name not in before_shunt or build_options._number(before_shunt, name) !=
               build_options._number(after_shunt, name) for name in shunt_macros):
            required.add('afe')
        return build_options.linked_update_groups(self.settings.get('update_groups', []), required)

    def save(self, edited: dict) -> None:
        # 同时保留摘要更新涉及的产品文件；保存失败按原字节回滚，不碰其它脏文件。
        groups = self.update_groups_for_changes(edited)
        paths = set(edited) | set(self.originals)
        originals = {p: p.read_bytes() for p in paths}
        for p in self.originals:
            if p.read_bytes() != self.originals[p]: raise ValueError("源码已被其它程序修改，请重新载入：" + str(p))
        written = {}
        try:
            for p, text in edited.items():
                raw = (b"\xef\xbb\xbf" if originals[p].startswith(b"\xef\xbb\xbf") else b"") + text.encode("utf-8")
                p.write_bytes(raw); written[p] = raw
            affected = (self.product,)
            for product in affected:
                result = build_options.save_source(ROOT, product, {'update_groups': groups})
                for relative in result["changed_files"]:
                    p = ROOT / relative
                    written[p] = p.read_bytes()
        except Exception:
            for p, raw in written.items():
                if p.read_bytes() == raw: p.write_bytes(originals[p])
            raise


class Window:
    def __init__(self, product="d014"):
        self.root = tk.Tk(); self.root.title("BMS 源码参数与 AFE 寄存器配置")
        self.root.geometry("1320x930")
        self.product = tk.StringVar(value=product)
        initial = build_options.read_source(ROOT, product, for_edit=True)
        self.chemistry = tk.StringVar(value=initial["chemistry"])
        self.search = tk.StringVar(); self.element = tk.StringVar(); self.parallel = tk.StringVar(); self.debug = tk.BooleanVar()
        self.customer_name=tk.StringVar();self.customer_revision=tk.StringVar(value='v1')
        self.capacity=tk.StringVar();self.cells=tk.StringVar();self.build_chemistry=tk.StringVar()
        self.only_configurable=tk.BooleanVar(value=True)
        self.parameter_group=tk.StringVar(value="全部参数")
        self.config = None; self.row_edits = {}; self.register_edits = {}
        top = ttk.Frame(self.root, padding=8); top.pack(fill="x")
        for name, variable, values in (("产品", self.product, build_options.PRODUCTS), ("参数表类型", self.chemistry, ("lfp", "nmc"))):
            ttk.Label(top, text=name).pack(side="left", padx=4)
            box = ttk.Combobox(top, textvariable=variable, values=values, state="readonly", width=12)
            box.pack(side="left"); box.bind("<<ComboboxSelected>>", self.reload)
        ttk.Button(top, text="重新载入", command=self.reload).pack(side="left", padx=8)
        ttk.Button(top, text="预览 Git 差异 / 保存源码", command=self.save).pack(side="right")
        profiles=ttk.Frame(self.root,padding=5);profiles.pack(fill='x')
        ttk.Label(profiles,text='客户/方案').pack(side='left');ttk.Entry(profiles,textvariable=self.customer_name,width=20).pack(side='left',padx=4)
        ttk.Label(profiles,text='版本').pack(side='left');ttk.Entry(profiles,textvariable=self.customer_revision,width=12).pack(side='left',padx=4)
        ttk.Button(profiles,text='另存客户方案 JSON',command=self.save_profile).pack(side='left',padx=5)
        ttk.Button(profiles,text='载入客户方案（先预览）',command=self.load_profile).pack(side='left',padx=5)
        self.status = ttk.Label(self.root, text=""); self.status.pack(fill="x", padx=12)
        self.tabs = ttk.Notebook(self.root); self.tabs.pack(fill="both", expand=True, padx=8, pady=8)
        self.parameters = ttk.Frame(self.tabs); self.registers = ttk.Frame(self.tabs); self.board = ttk.Frame(self.tabs)
        self.tabs.add(self.parameters, text="软件 / AFE 硬件保护参数")
        self.tabs.add(self.registers, text="原厂寄存器 / 每一位")
        self.tabs.add(self.board, text="分流器 / 开发调试")
        reference_tab = ttk.Frame(self.tabs)
        self.tabs.add(reference_tab, text="原厂未决说明")
        reference_text = tk.Text(reference_tab, wrap="word")
        reference_text.pack(fill="both", expand=True)
        reference_text.insert("end", (ROOT / "references/afe/CORRECTIONS.md").read_text(encoding="utf-8") + "\n\n")
        for family in CATALOG["families"]:
            reference_text.insert("end", family["model"] + " " + family["revision"] + "\n" +
                                  "\n".join(str(item) for item in family["issues"]) + "\n\n")
        reference_text.config(state="disabled")
        parameter_bar=ttk.Frame(self.parameters,padding=6);parameter_bar.pack(fill="x")
        ttk.Label(parameter_bar,text="参数分类").pack(side="left")
        groups=ttk.Combobox(parameter_bar,textvariable=self.parameter_group,state="readonly",width=20,
            values=("全部参数","软件保护","AFE 硬件保护","休眠 / 加热 / 均衡","SOC / 容量"))
        groups.pack(side="left",padx=8);groups.bind("<<ComboboxSelected>>",lambda _:self.populate())
        ttk.Label(parameter_bar,text="双击数值编辑；电流 A、温度 ℃、休眠分钟，保存时精确换回源码单位。").pack(side="left")
        self.table = self.tree(self.parameters, ("名称", "当前源码", "修改值", "单位", "影响范围", "宏"), (280,100,100,70,100,330))
        self.table.bind("<Double-1>", self.edit_parameter)
        bar = ttk.Frame(self.registers); bar.pack(fill="x")
        ttk.Label(bar, text="搜索地址 / 寄存器名 / 位名").pack(side="left")
        ttk.Entry(bar, textvariable=self.search, width=40).pack(side="left", padx=8)
        ttk.Checkbutton(bar,text='只看本窗口可配置项',variable=self.only_configurable,command=self.load_registers).pack(side='left')
        ttk.Button(bar, text="打开对应原厂 PDF", command=self.open_pdf).pack(side="right")
        self.search.trace_add("write", lambda *_: self.load_registers())
        self.regtable = self.tree(self.registers, ("地址", "寄存器", "复位值", "源码预设", "可写掩码", "原页"), (70,380,90,140,100,200), height=12)
        self.regtable.bind("<<TreeviewSelect>>", self.show_bits)
        self.bits = self.tree(self.registers, ("字段", "位掩码", "属性", "源码编码", "配置入口", "含义"), (135,75,75,110,170,490), height=8)
        self.bits.bind("<Double-1>", self.edit_bit)
        self.bits.bind('<<TreeviewSelect>>',self.show_field_help)
        self.field_details=tk.Text(self.registers,height=5,wrap='word');self.field_details.pack(fill='x')
        ttk.Label(self.registers, text="双击配置字段，可用 bit 按钮或原始编码编辑。保护字段会换算回唯一的物理参数；显示值为源码预计编码，不是设备读回。取消筛选可查看所有只读/运行时项。", wraplength=1200).pack(fill="x")
        ttk.Button(self.registers, text="为选中字段生成内部版调试文件", command=self.export_debug).pack(anchor="e", pady=5)
        ttk.Button(self.registers, text="跳到此寄存器的硬件保护参数", command=self.goto_parameter).pack(anchor="e", pady=3)
        ttk.Button(self.registers, text="取消此寄存器的源码覆盖", command=self.remove_preset).pack(anchor="e", pady=3)
        info = ttk.Frame(self.board, padding=18); info.pack(fill="x")
        assembly=ttk.Frame(self.board,padding=12);assembly.pack(fill='x')
        for name,var in (('串数',self.cells),('容量（0.1Ah）',self.capacity)):
            ttk.Label(assembly,text=name).pack(side='left');ttk.Entry(assembly,textvariable=var,width=10).pack(side='left',padx=6)
        ttk.Label(assembly,text='编译电池类型').pack(side='left');ttk.Combobox(assembly,textvariable=self.build_chemistry,values=('lfp','nmc'),state='readonly',width=10).pack(side='left',padx=6)
        ttk.Label(info, text="计算电阻 = 分子（单只 µΩ） / 分母（并联只数）；未知装配组合时填写等效值 / 1。").grid(row=0,column=0,columnspan=3,sticky="w",pady=8)
        for row, (name, var) in enumerate((("单只电阻 / 分子 µΩ", self.element), ("并联只数 / 分母", self.parallel)), 1):
            ttk.Label(info,text=name).grid(row=row,column=0,sticky="w"); ttk.Entry(info,textvariable=var).grid(row=row,column=1,pady=8)
        self.shunt_info = ttk.Label(info,text=""); self.shunt_info.grid(row=3,column=0,columnspan=3,sticky="w",pady=12)
        self.element.trace_add("write",lambda *_: self.update_shunt()); self.parallel.trace_add("write",lambda *_: self.update_shunt())
        ttk.Checkbutton(info,text="启用开发寄存器协议（生产构建禁止；默认关闭）",variable=self.debug).grid(row=4,column=0,columnspan=3,sticky="w")
        ttk.Label(info,text="保存源码不改变设备已有参数。OTA 分组与一次性标识继续由原编译窗口管理；本窗口不构建、不 OTA。\n软件保护与 AFE profile 独立。原始寄存器临时修改不写 Flash，恢复后重新应用源码固定配置和设备持久 profile。",wraplength=1100).grid(row=5,column=0,columnspan=3,sticky="w",pady=20)
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.reload()

    @staticmethod
    def tree(parent, columns, widths, height=20):
        box=ttk.Frame(parent); box.pack(fill="both",expand=True)
        tree=ttk.Treeview(box,columns=columns,show="headings",height=height)
        for column,width in zip(columns,widths): tree.heading(column,text=column); tree.column(column,width=width,minwidth=60)
        scrollbar=ttk.Scrollbar(box,orient="vertical",command=tree.yview); tree.configure(yscrollcommand=scrollbar.set)
        horizontal=ttk.Scrollbar(box,orient="horizontal",command=tree.xview); tree.configure(xscrollcommand=horizontal.set)
        tree.grid(row=0,column=0,sticky="nsew");scrollbar.grid(row=0,column=1,sticky="ns");horizontal.grid(row=1,column=0,sticky="ew")
        box.rowconfigure(0,weight=1);box.columnconfigure(0,weight=1)
        return tree

    def has_edits(self):
        if not self.config:return False
        element,parallel,debug,presets,name,revision=self.config.baseline_extra
        return (self.config.parameters_changed() or self.register_edits or self.config.presets!=presets or
                self.element.get()!=str(element) or self.parallel.get()!=str(parallel) or self.debug.get()!=debug or
                self.customer_name.get()!=name or self.customer_revision.get() not in (revision, 'v1' if not revision else revision) or
                self.cells.get()!=str(self.config.baseline_settings['cell_count']) or
                self.capacity.get()!=str(self.config.baseline_settings['capacity_0p1ah']) or
                self.build_chemistry.get()!=self.config.baseline_settings['chemistry'])

    def reload(self, event=None):
        if self.config and event is not None and self.product.get()==self.config.product:
            try:self.config.change_chemistry(self.chemistry.get());self.populate();return
            except Exception as error:messagebox.showerror('切换失败',str(error));return
        if self.has_edits() and not messagebox.askyesno("未保存修改", "放弃本地修改并重新载入？"):
            self.product.set(self.config.product); self.chemistry.set(self.config.chemistry); return
        try:
            if self.config and self.product.get() != self.config.product:
                self.chemistry.set(build_options.read_source(ROOT, self.product.get(), for_edit=True)["chemistry"])
            self.config = Configuration(self.product.get(),self.chemistry.get());self.register_edits={}
            self.element.set(str(self.config.element));self.parallel.set(str(self.config.parallel));self.debug.set(self.config.debug)
            self.customer_name.set(self.config.customer_name);self.customer_revision.set(self.config.customer_revision or 'v1')
            self.cells.set(str(self.config.settings['cell_count']));self.capacity.set(str(self.config.settings['capacity_0p1ah']));self.build_chemistry.set(self.config.settings['chemistry'])
            self.populate()
        except Exception as error: messagebox.showerror("载入失败",str(error))

    def populate(self):
        self.table.delete(*self.table.get_children())
        group=self.parameter_group.get()
        for index,row in enumerate(self.config.rows):
            name=row['name']
            category=("软件保护" if name.startswith(('BMS_SW_','D008_SW_')) else
                "AFE 硬件保护" if name.startswith(('D008_AFE_','SH3673510_HW_DEFAULT_')) else
                "休眠 / 加热 / 均衡" if name.startswith(('BMS_SLEEP_','BMS_HEATER_','BMS_BALANCE_')) else "SOC / 容量")
            if group!="全部参数" and group!=category:continue
            self.table.insert('','end',iid=str(index),values=(label(name),display_value(name,row['value']),display_value(name,row['edit']),unit(name),row['scope'],name))
        self.status.config(text=f"{self.config.product.upper()} 独立参数 · {self.config.settings['afe_model']} · 正在编辑 {self.config.chemistry.upper()} 参数表 · {self.config.family['revision']} · 目录 {CATALOG['catalog_id']} · 尚未写入源码")
        self.load_registers()

    def collect(self):
        options={**self.config.settings,'cell_count':integer(self.cells.get()),'capacity_0p1ah':integer(self.capacity.get()),'chemistry':self.build_chemistry.get()}
        self.config.settings=build_options.normalize(options,self.config.product,'custom' if self.config.product=='d008' else None)
        self.config.customer_name=self.customer_name.get().strip();self.config.customer_revision=self.customer_revision.get().strip() if self.customer_name.get().strip() else ''
        return integer(self.element.get()),integer(self.parallel.get()),self.debug.get()

    def save_profile(self):
        try:
            element,parallel,debug=self.collect()
            data=self.config.profile(element,parallel,debug,self.customer_name.get(),self.customer_revision.get())
            folder=ROOT/'configurations/customers'/self.config.product;folder.mkdir(parents=True,exist_ok=True)
            filename=re.sub(r'[<>:"/\\|?*\x00-\x1f]','_',data['name']+'-'+data['revision'])+'.json'
            path=filedialog.asksaveasfilename(parent=self.root,defaultextension='.json',filetypes=[('客户配置方案','*.json')],initialdir=str(folder),initialfile=filename)
            if not path:return
            Path(path).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
            messagebox.showinfo('方案已保存','已保存可重复载入的客户方案，包含两种化学体系的参数。尚未修改源码，也没有编译或写设备。')
        except Exception as error:messagebox.showerror('保存方案失败',str(error))

    def load_profile(self):
        path=filedialog.askopenfilename(parent=self.root,filetypes=[('客户配置方案','*.json')],initialdir=str(ROOT/'configurations/customers'))
        if not path:return
        try:
            file=Path(path)
            if file.stat().st_size>262144:raise ValueError('方案文件超过 256KiB')
            def unique(pairs):
                result={}
                for key,value in pairs:
                    if key in result:raise ValueError('JSON 含重复字段：'+key)
                    result[key]=value
                return result
            data=json.loads(file.read_text(encoding='utf-8-sig'),object_pairs_hook=unique)
            candidate=Configuration(self.product.get(),self.chemistry.get());candidate.load_profile(data)
            if self.has_edits() and not messagebox.askyesno('待编辑配置','用客户方案替换窗口内未保存的修改？源码仍不会立即改变。'):return
            self.config=candidate;self.chemistry.set(candidate.chemistry);self.element.set(str(candidate.element));self.parallel.set(str(candidate.parallel));self.debug.set(candidate.debug)
            self.customer_name.set(candidate.customer_name);self.customer_revision.set(candidate.customer_revision)
            self.cells.set(str(candidate.settings['cell_count']));self.capacity.set(str(candidate.settings['capacity_0p1ah']));self.build_chemistry.set(candidate.settings['chemistry'])
            self.register_edits={'profile':True};self.populate()
            messagebox.showinfo('方案已载入','已载入窗口，源码尚未修改。检查参数、分流器和装配后，点击“预览 Git 差异 / 保存源码”。设备覆盖组仍在原编译窗口明确选择。')
        except Exception as error:messagebox.showerror('载入方案失败',str(error))

    def update_shunt(self):
        try:
            resistance=Fraction(integer(self.element.get()),integer(self.parallel.get()))
            if resistance<=0: raise ValueError()
            text=f"精确模型 {resistance} µΩ = {float(resistance)/1000:.9f} mΩ；整数 µΩ 仅供旧协议显示。"
            if self.product.get()!="d008":
                text+=f" OCD1 最低 {float(5000/resistance):.6f} A，OCD2 最低 {float(10000/resistance):.6f} A；SC=OCD2×2/3/4/6。"
            self.shunt_info.config(text=text)
        except (ValueError,SyntaxError,ZeroDivisionError): self.shunt_info.config(text="请输入有效整数比例")

    def edit_parameter(self,event):
        item=self.table.identify_row(event.y)
        if not item:return
        row=self.config.rows[int(item)]
        from tkinter.simpledialog import askstring
        value=askstring(label(row["name"]),f"单位：{unit(row['name'])}；范围与恢复关系由固件继续校验。\n影响：{row['scope']}\n{row['name']}",initialvalue=display_value(row['name'],row['edit']),parent=self.root)
        if value is not None:
            try:row["edit"]=source_value(row['name'],value);values=list(self.table.item(item,"values"));values[2]=display_value(row['name'],row['edit']);self.table.item(item,values=values);self.load_registers()
            except Exception as error:messagebox.showerror("无效值",str(error))

    def load_registers(self):
        if not self.config:return
        selected=self.regtable.selection(); self.regtable.delete(*self.regtable.get_children()); self.bits.delete(*self.bits.get_children())
        query=self.search.get().lower()
        codec=SourceProtectionFields(self.config)
        for register in self.config.family["registers"]:
            text=json.dumps(register,ensure_ascii=False).lower()
            if query and query not in text and query not in f"0x{register['address']:02x}":continue
            address=register["address"]; preset=self.config.presets.get(address)
            configurable=any(not f['reserved'] and (codec.owns(address,f['mask']) or f['mask'] & ~register['source_mask']==0) for f in register['fields'])
            if self.only_configurable.get() and not configurable:continue
            pages=" / ".join(str(s["pdf_page"]) for s in register["sources"])
            self.regtable.insert("","end",iid=str(address),values=(f"0x{address:02X}",register["name"],register["reset"] or "未规定",f"mask={preset[0]:02X}, value={preset[1]:02X}" if preset else "未覆盖",f"0x{register['write_mask']:02X}",pages))
        if selected and self.regtable.exists(selected[0]):self.regtable.selection_set(selected[0])

    def selected_register(self):
        selection=self.regtable.selection()
        if not selection:raise ValueError("请先选择寄存器")
        return next(r for r in self.config.family["registers"] if r["address"]==int(selection[0]))

    def show_bits(self,event=None):
        self.bits.delete(*self.bits.get_children())
        try:register=self.selected_register()
        except ValueError:return
        preset=self.config.presets.get(register["address"],(0,0))
        codec=SourceProtectionFields(self.config)
        try:encoded=SourceProtectionFields(self.config,integer(self.element.get()),integer(self.parallel.get())).registers()
        except Exception:encoded={}
        for index,field in enumerate(register["fields"]):
            route=self.field_route(register,field,codec)
            value=str((preset[1]&field["mask"])>>field["shift"]) if preset[0]&field["mask"]==field["mask"] else '未设预设'
            if route=='保护参数 ↔ 编码' and encoded.get(register['address']) is not None:value=str((encoded[register['address']]&field['mask'])>>field['shift'])+'（预计）'
            self.bits.insert("","end",iid=str(index),values=(field["name"],f"0x{field['mask']:02X}","保留" if field["reserved"] else field["access"],value,route,field['description']))

    @staticmethod
    def field_route(register,field,codec):
        if field['reserved']:return '保留位，不能配置'
        if field['access'] not in ('RW','R/W'):return '只读/清标志，不能预设'
        if codec.owns(register['address'],field['mask']):return '保护参数 ↔ 编码'
        if field['mask'] & ~register['source_mask']==0:return '固定字段源码预设'
        if field['mask'] & ~register['write_mask']==0:return '仅内部版临时调试'
        return '板级/运行时专用入口'

    def show_field_help(self,event=None):
        self.field_details.config(state='normal');self.field_details.delete('1.0','end')
        try:
            register=self.selected_register();selection=self.bits.selection()
            if selection:
                field=register['fields'][int(selection[0])]
                self.field_details.insert('end',f"0x{register['address']:02X} {field['name']}\n"+field_help(field)+'\n配置入口：'+self.field_route(register,field,SourceProtectionFields(self.config)))
        except ValueError:pass
        self.field_details.config(state='disabled')

    def ask_field(self,source=False):
        register=self.selected_register(); selection=self.bits.selection()
        if not selection:raise ValueError("请先选择一个字段")
        field=register["fields"][int(selection[0])]
        policy=register["source_mask"] if source else register["write_mask"]
        if field["mask"]&~policy:raise ValueError("此字段属于保护 profile、运行时命令、只读、保留或有副作用字段。保护参数请用参数页；命令不做通用位写入。")
        from tkinter.simpledialog import askinteger
        maximum=field["mask"]>>field["shift"]
        value=askinteger(field["name"],field["description"]+f"\n输入原始字段编码 0..{maximum}",minvalue=0,maxvalue=maximum,parent=self.root)
        return register,field,value

    def edit_bit(self,event):
        try:
            item=self.bits.identify_row(event.y)
            if not item:return
            self.bits.selection_set(item)
            register=self.selected_register();field=register['fields'][int(item)]
            codec=SourceProtectionFields(self.config,integer(self.element.get()),integer(self.parallel.get()))
            route=self.field_route(register,field,codec)
            editable=route in ('保护参数 ↔ 编码','固定字段源码预设')
            current=None
            if route=='保护参数 ↔ 编码':
                raw=codec.registers().get(register['address'])
                if raw is not None:current=(raw&field['mask'])>>field['shift']
            elif editable:
                mask,value=self.config.presets.get(register['address'],(0,0))
                if mask&field['mask']==field['mask']:current=(value&field['mask'])>>field['shift']
            dialog=tk.Toplevel(self.root);dialog.title(f"0x{register['address']:02X} {field['name']} · {route}");dialog.geometry('760x530')
            ttk.Label(dialog,text=route+'；修改只进入窗口，保存源码后才生效。',wraplength=720).pack(padx=12,pady=8,anchor='w')
            helpbox=tk.Text(dialog,height=11,wrap='word');helpbox.pack(fill='both',expand=True,padx=10)
            helpbox.insert('end',field_help(field)+'\n原 PDF 页：'+' / '.join(str(s['pdf_page']) for s in register['sources']));helpbox.config(state='disabled')
            maximum=field['mask']>>field['shift'];code=tk.StringVar(value=str(current if current is not None else 0))
            ttk.Label(dialog,text=('当前源码预计编码：'+str(current)) if current is not None else '未设置源码覆盖；输入初值 0 不是设备读回，也不是原源码值。').pack(pady=5)
            bitbar=ttk.Frame(dialog);bitbar.pack(pady=8);variables=[];updating=[False]
            def update_bits(*_):
                if updating[0]:return
                try:
                    value=integer(code.get())
                    if not 0<=value<=maximum:return
                    for bit,var in variables:var.set(bool(value&(1<<bit)))
                except (ValueError,SyntaxError):pass
            def update_code():
                updating[0]=True;code.set(str(sum((1<<bit) for bit,var in variables if var.get())));updating[0]=False
            for bit in range(maximum.bit_length()-1,-1,-1):
                var=tk.BooleanVar(value=bool((current or 0)&(1<<bit)));variables.append((bit,var))
                box=ttk.Checkbutton(bitbar,text=f"bit{bit+field['shift']}",variable=var,command=update_code)
                box.pack(side='left',padx=5)
                if not editable:box.state(['disabled'])
            entry=ttk.Entry(dialog,textvariable=code,width=22);entry.pack(pady=5);code.trace_add('write',update_bits)
            if route=='保护参数 ↔ 编码':
                preview=ttk.Label(dialog,wraplength=720);preview.pack(padx=10,pady=5)
                def physical_preview(*_):
                    try:
                        candidate=copy.copy(self.config);candidate.rows=[dict(row) for row in self.config.rows]
                        proposed=SourceProtectionFields(candidate,integer(self.element.get()),integer(self.parallel.get()))
                        proposed.edit(register['address'],field['mask'],field['shift'],integer(code.get()))
                        changes=[f"{label(row['name'])} = {display_value(row['name'],row['edit'])} {unit(row['name'])}"
                            for row,old in zip(candidate.rows,self.config.rows) if row['edit']!=old['edit']]
                        preview.config(text='源码参数预览：'+('；'.join(changes) if changes else '与当前待编辑值相同'))
                    except Exception as error:preview.config(text='该编码不能应用：'+str(error))
                code.trace_add('write',physical_preview);physical_preview()
            if not editable:
                entry.state(['disabled'])
                ttk.Label(dialog,text='当前值未知，位按钮仅表示位位置。该项不能当作静态客户参数。只读/状态/清除/命令由硬件或后端拥有；可在内部版读取状态，稳定 RW 字段可导出临时调试文件。',wraplength=720).pack(padx=10,pady=8)
            def apply():
                try:
                    value=integer(code.get())
                    if not 0<=value<=maximum:raise ValueError(f'编码范围为 0..{maximum}')
                    if route=='保护参数 ↔ 编码':codec.edit(register['address'],field['mask'],field['shift'],value)
                    else:
                        address=register['address'];mask,previous=self.config.presets.get(address,(0,0))
                        self.config.presets[address]=(mask|field['mask'],(previous&~field['mask'])|(value<<field['shift']))
                        self.register_edits[address]=True
                    dialog.destroy();self.populate();self.regtable.selection_set(str(register['address']));self.show_bits()
                except Exception as error:messagebox.showerror('该编码未应用',str(error),parent=dialog)
            if editable:ttk.Button(dialog,text='应用到待编辑配置',command=apply).pack(pady=10)
            ttk.Button(dialog,text='关闭',command=dialog.destroy).pack(pady=5)
            dialog.transient(self.root);dialog.grab_set()
        except (ValueError,KeyError,SyntaxError,ZeroDivisionError) as error:messagebox.showinfo("字段配置",str(error))

    def export_debug(self):
        try:
            register,field,value=self.ask_field()
            if value is None:return
            path=filedialog.asksaveasfilename(defaultextension=".json",initialfile=f"{self.config.product}-afe-debug.json",initialdir=str(Path(os.environ.get("USERPROFILE",str(Path.home()))) / "Documents"))
            if path:
                Path(path).write_text(json.dumps(dict(schema=1,catalog_id=CATALOG["catalog_id"],model=self.config.settings["afe_model"],registers=[dict(address=register["address"],mask=field["mask"],value=value<<field["shift"]) ]),ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        except ValueError as error:messagebox.showerror("不能导出",str(error))

    def remove_preset(self):
        try:
            address = self.selected_register()["address"]
            if address in self.config.presets:
                del self.config.presets[address]
                self.register_edits[address] = True
                self.load_registers()
        except ValueError as error:
            messagebox.showinfo("源码预设", str(error))

    def goto_parameter(self):
        try:
            address = self.selected_register()["address"]
            if self.config.product == "d008":
                key = {0x59:"OCD1",0x5A:"OCC1",0x5B:"OCD1",0x5C:"OCC1",0x5E:"OCD2",0x5F:"OCC2",0x60:"OCD2",0x61:"OCC2",0x62:"SC_",0x63:"SC_",0x70:"COV",0x71:"COV",0x72:"CUV",0x73:"CUV"}.get(address)
            else:
                key = {0x49:"COV",0x4A:"COV",0x4B:"CUV",0x4C:"CUV",0x4D:"OCD1",0x4E:"OCD2",0x4F:"SC_",0x50:"OCC1",0x51:"CHG_OT",0x52:"DSG_OT",0x53:"CHG_UT",0x54:"DSG_UT"}.get(address)
            matches = [str(i) for i, row in enumerate(self.config.rows)
                       if key and key in row["name"] and ("HW_DEFAULT" in row["name"] or row["name"].startswith("D008_AFE_"))]
            if not matches:
                raise ValueError("该寄存器没有独立持久保护字段。固定字段可设置源码预设；运行时命令/RO/保留位只查阅或用原业务入口。")
            self.tabs.select(self.parameters)
            self.parameter_group.set("AFE 硬件保护");self.populate()
            self.table.selection_set(matches)
            self.table.see(matches[0])
        except ValueError as error:
            messagebox.showinfo("字段所有权", str(error))

    def open_pdf(self):
        try:
            source=self.selected_register()["sources"][-1]
            path=ROOT/REFERENCE/"sources"/source.get("filename",source.get("file"))
            os.startfile(str(path));messagebox.showinfo("原厂依据",f"请定位 PDF 第 {source['pdf_page']} 页。原文件：{path.name}")
        except Exception as error:messagebox.showerror("打开失败",str(error))

    def save(self):
        if not self.has_edits():
            messagebox.showinfo("源码配置", "没有修改"); return
        try:
            edited=self.config.replacements(*self.collect())
            if not edited:messagebox.showinfo("源码配置","没有修改");return
            preview=tk.Toplevel(self.root);preview.title("保存前差异（不编译、不 OTA）");preview.geometry("1100x650")
            groups=self.config.update_groups_for_changes(edited)
            ttk.Label(preview,text='保存后 OTA 更新组：'+(' / '.join(groups) if groups else '保留设备参数')+
                      '；更新组和固定标识会随本次源码修改一起保存。',wraplength=1050).pack(padx=10,pady=8)
            text=tk.Text(preview,wrap="none");text.pack(fill="both",expand=True)
            for path,after in edited.items():
                before=self.config.originals[path].decode("utf-8-sig")
                text.insert("end","".join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile=str(path.relative_to(ROOT)),tofile=str(path.relative_to(ROOT)))))
            text.config(state="disabled")
            def commit():
                try:self.config.save(edited);preview.destroy();self.config=None;self.reload();messagebox.showinfo("已保存","源码已保存，OTA 摘要/标识按原规则同步。请审查 Git 差异；本次未构建或连接设备。")
                except Exception as error:messagebox.showerror("保存失败",str(error))
            ttk.Button(preview,text="确认保存这些源码修改",command=commit).pack(pady=8)
        except Exception as error:messagebox.showerror("配置无效",str(error))

    def close(self):
        if not self.has_edits() or messagebox.askyesno("未保存修改","放弃本地修改并关闭？"):self.root.destroy()


if __name__ == "__main__":
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--product",choices=build_options.PRODUCTS,default="d014")
    Window(parser.parse_args().product).root.mainloop()

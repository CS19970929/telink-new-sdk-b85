"""D008 源码参数、客户方案与 DVC1124-2 寄存器窗口。"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from decimal import Decimal
import difflib
from fractions import Fraction
import json
from pathlib import Path
import re
import subprocess
import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, simpledialog, ttk
import webbrowser

import build_options
import dvc_source_fields


ROOT = Path(__file__).resolve().parents[1]
REFERENCE = Path("references/afe/AFE_完整开发参考库_20261005")
PRODUCT = "d008"
PRODUCT_DIR = "bms/products/d008/"
PROTECTION = PRODUCT_DIR + "d008_protection_config.h"
BOARD = PRODUCT_DIR + "bms_product.h"
SHUNT = PRODUCT_DIR + "dvc1124_product_defaults.h"
CUSTOMER = PRODUCT_DIR + "afe_register_config.h"
NUMBER = r"(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*"
DEFINE = re.compile(r"(?m)^[ \t]*#define[ \t]+(\w+)(\(c\))?[ \t]+([^\r\n]+)")
TERNARY = re.compile(r"\(\(c\) == BMS_SOC_CHEMISTRY_LFP \? (" + NUMBER + r") : (" + NUMBER + r")\)")
GROUPS = {
    "软件保护": ("D008_SW_",),
    "AFE 硬件保护": ("D008_AFE_",),
    "加热与均衡": ("BMS_HEATER_", "BMS_BALANCE_", "BMS_BATTERY_BALANCE_"),
    "低功耗": ("BMS_SLEEP_",),
}
ASSEMBLY_KEYS = ("afe_model", "cell_count", "chemistry", "capacity_0p1ah")


def number(text: str) -> int:
    text = text.rstrip("uUlL")
    return int(text, 16 if text.lower().startswith("0x") else 10)


def units(name: str) -> tuple[Fraction, int, str]:
    name = name.removesuffix("_FOR").removesuffix("_DEFAULT")
    if name.endswith("_A10"):
        return Fraction(1, 10), 0, "A"
    if name.endswith("_X10"):
        return Fraction(1, 10), -40, "℃"
    if name.endswith("_10MS"):
        return Fraction(10), 0, "ms"
    if name.startswith("BMS_SLEEP_") and name.endswith("_SECONDS"):
        return Fraction(1, 60), 0, "min"
    return Fraction(1), 0, next((unit for suffix, unit in (
        ("_MV", "mV"), ("_MS", "ms"), ("_US", "µs"), ("_PERCENT", "%"))
        if name.endswith(suffix)), "")


def display(name: str, value: int) -> str:
    scale, offset, _ = units(name)
    physical = value * scale + offset
    if physical.denominator in (1, 2, 5, 10):
        return format(Decimal(physical.numerator) / physical.denominator, "f")
    return str(physical)


def parameter_label(name: str) -> str:
    name = name.removesuffix("_FOR").removesuffix("_DEFAULT")
    for prefix in ("D008_SW_", "D008_AFE_", "BMS_BATTERY_", "BMS_"):
        if name.startswith(prefix):
            name = name.removeprefix(prefix)
            break
    replacements = {
        "CELL_OVP": "单体过压", "CELL_UVP": "单体欠压", "PACK_OVP": "总压过压",
        "PACK_UVP": "总压欠压", "DISCHARGE_OCP": "放电过流", "CHARGE_OCP": "充电过流",
        "DISCHARGE_OTP": "放电高温", "CHARGE_OTP": "充电高温", "DISCHARGE_UTP": "放电低温",
        "CHARGE_UTP": "充电低温", "COV": "单体过压", "CUV": "单体欠压",
        "OCD1": "放电过流1", "OCD2": "放电过流2", "OCC1": "充电过流1", "OCC2": "充电过流2",
        "FIRST": "一级", "SECOND": "二级", "THIRD": "三级", "RECOVER": "恢复",
        "FILTER": "滤波", "HEATER": "加热", "BALANCE": "均衡", "SLEEP": "休眠",
        "ENABLE": "使能", "DELAY": "延时", "SC_": "短路_",
    }
    for field, label in replacements.items():
        name = name.replace(field, label)
    return name.replace("_", " ")


@dataclass
class Parameter:
    file: str
    category: str
    tokens: dict[str, tuple[int, int, int]]


class SourceDefaults:
    def __init__(self, root: Path):
        self.root = root
        self.source = build_options.read_source(root, PRODUCT, for_edit=True)
        self.assembly = {key: self.source[key] for key in ASSEMBLY_KEYS}
        self.original = {path: (root / path).read_bytes() for path in (PROTECTION, BOARD, SHUNT, CUSTOMER)}
        self.parameters = {}
        self.values = {}
        for path in (PROTECTION, BOARD):
            for match in DEFINE.finditer(self.text(path)):
                name, argument, expression = match.groups()
                category = next((key for key, prefixes in GROUPS.items() if name.startswith(prefixes)), None)
                if not category:
                    continue
                expression = expression.split("/*", 1)[0].split("//", 1)[0].strip()
                if argument:
                    pair = TERNARY.fullmatch(expression)
                    if not pair:
                        continue
                    base = match.start(3)
                    tokens = {chemistry: (base + pair.start(i), base + pair.end(i), number(pair[i]))
                              for i, chemistry in ((1, "lfp"), (2, "nmc"))}
                elif re.fullmatch(NUMBER, expression):
                    tokens = {"common": (match.start(3), match.start(3) + len(expression), number(expression))}
                else:
                    continue
                if name in self.parameters:
                    raise ValueError("源码参数定义重复：" + name)
                self.parameters[name] = Parameter(path, category, tokens)
                self.values[name] = {branch: token[2] for branch, token in tokens.items()}
        self.element = build_options._number(self.text(SHUNT), "DVC1124_DEFAULT_SHUNT_UOHM")
        self.parallel = 1
        customer = build_options.source_customer_profile(root, PRODUCT)
        self.name = customer.get("name", "")
        self.revision = customer.get("revision", "")

    def text(self, file: str) -> str:
        return self.original[file].decode("utf-8-sig")

    def get(self, name: str, chemistry: str) -> int:
        values = self.values[name if name in self.values else name + "_FOR"]
        return values[chemistry] if chemistry in values else values["common"]

    def set(self, name: str, chemistry: str, value: int) -> None:
        limit = 86400 * 365 if name.endswith("_SECONDS") else 65535
        if "ENABLE_" in name:
            limit = 1
        if type(value) is not int or not 0 <= value <= limit:
            raise ValueError(f"{name} 的源码整数须为 0～{limit}。")
        values = self.values[name if name in self.values else name + "_FOR"]
        values[chemistry if chemistry in values else "common"] = value

    def shunt(self) -> int:
        if not (1 <= self.element <= 60000 and 1 <= self.parallel <= 16) or self.element % self.parallel:
            raise ValueError("分流电阻须能精确换算为整数 µΩ。")
        return self.element // self.parallel

    def changes(self, linked_updates: bool) -> tuple[dict, dict]:
        shunt = self.shunt()
        for label in (self.name, self.revision):
            if len(label) > 80 or any(ord(char) < 32 for char in label):
                raise ValueError("客户/方案名称与版本最多 80 个普通字符。")
        edits = {path: [] for path in self.original}
        needed = set()
        for name, parameter in self.parameters.items():
            for branch, (start, end, original) in parameter.tokens.items():
                value = self.values[name][branch]
                if value == original:
                    continue
                edits[parameter.file].append((start, end, f"{value}u"))
                group = {"软件保护": "sw", "AFE 硬件保护": "afe", "加热与均衡": "business"}.get(parameter.category)
                if group:
                    needed.add(group)
        candidate = {file: self.text(file) for file in self.original}
        for file, replacements in edits.items():
            for start, end, value in sorted(replacements, reverse=True):
                candidate[file] = candidate[file][:start] + value + candidate[file][end:]
        if shunt != build_options._number(candidate[SHUNT], "DVC1124_DEFAULT_SHUNT_UOHM"):
            candidate[SHUNT] = build_options._replace(candidate[SHUNT], "DVC1124_DEFAULT_SHUNT_UOHM", f"{shunt}u")
            needed.add("afe")
        for macro, value in (("BMS_SOURCE_CUSTOMER_NAME", self.name), ("BMS_SOURCE_CUSTOMER_REVISION", self.revision)):
            pattern = r'(?m)^#define[ \t]+' + macro + r'[ \t]+[^\r\n]*'
            line = f"#define {macro} {json.dumps(value, ensure_ascii=False)}"
            if re.search(pattern, candidate[CUSTOMER]):
                candidate[CUSTOMER] = re.sub(pattern, lambda _: line, candidate[CUSTOMER], count=1)
            elif value:
                candidate[CUSTOMER] += line + "\n"
        if self.assembly["capacity_0p1ah"] != self.source["capacity_0p1ah"]:
            needed.add("business")
        selected = self.source.get("update_groups", [])
        if linked_updates and "factory_reset" not in selected:
            selected = [group for group in build_options.UPDATE_GROUPS if group in needed or group in selected]
        config = dict(self.assembly, update_groups=selected)
        build_options.normalize(self.assembly, PRODUCT, "custom")
        return config, {file: (self.original[file], text) for file, text in candidate.items()
                        if text != self.text(file)}

    def scheme(self) -> dict:
        self.changes(False)
        if not self.name or not self.revision:
            raise ValueError("填写客户/方案名称与版本后再保存方案。")
        return {"schema": 1, "product": PRODUCT, "afe_model": "dvc1124", "name": self.name,
                "revision": self.revision, "assembly": self.assembly,
                "parameters": {name: values.get("common") if "common" in values else values
                               for name, values in self.values.items()},
                "shunt": {"element_uohm": self.element, "parallel_count": self.parallel}}

    def load_scheme(self, data: dict) -> list[str]:
        if (not isinstance(data, dict) or data.get("schema") != 1 or data.get("product") != PRODUCT
                or data.get("afe_model") != "dvc1124"):
            raise ValueError("请选择 D008 / DVC1124 的客户方案。")
        self.assembly = build_options.normalize(data["assembly"], PRODUCT, "custom")
        if set(self.assembly) != set(ASSEMBLY_KEYS):
            raise ValueError("客户方案装配字段不完整。")
        self.name, self.revision = data["name"].strip(), data["revision"].strip()
        self.element = data["shunt"]["element_uohm"]
        self.parallel = data["shunt"]["parallel_count"]
        if type(self.element) is not int or type(self.parallel) is not int:
            raise ValueError("分流器字段须为整数。")
        skipped = []
        for name, value in data["parameters"].items():
            if name not in self.parameters:
                skipped.append(name)
            elif set(self.values[name]) == {"common"}:
                self.set(name, "common", value)
            elif isinstance(value, dict) and set(value) == {"lfp", "nmc"}:
                for chemistry, raw in value.items():
                    self.set(name, chemistry, raw)
            else:
                raise ValueError("方案缺少两种化学体系的参数值：" + name)
        if data.get("debug_enabled") or data.get("presets"):
            skipped.append("旧版运行态调试与固定寄存器预设")
        self.changes(False)
        return skipped


def table(parent, columns):
    box = ttk.Frame(parent)
    box.pack(fill="both", expand=True, padx=5, pady=5)
    tree = ttk.Treeview(box, columns=[name for name, _ in columns], show="headings", selectmode="browse")
    for name, width in columns:
        tree.heading(name, text=name)
        tree.column(name, width=width)
    scroll = ttk.Scrollbar(box, orient="vertical", command=tree.yview)
    tree.configure(yscrollcommand=scroll.set)
    scroll.pack(side="right", fill="y")
    tree.pack(side="left", fill="both", expand=True)
    return tree


class Window:
    def __init__(self, root: tk.Tk, directory: Path):
        self.root = root
        self.directory = directory
        self.model = SourceDefaults(directory)
        with (directory / REFERENCE / "chips/dvc1124_2/registers.json").open(encoding="utf-8") as stream:
            catalog = json.load(stream)
        self.registers = {reg["address"]: reg for reg in catalog["registers"]}
        self.semantics = catalog["field_semantics"]
        root.title("D008 源码保护参数与 AFE 寄存器配置")
        root.geometry("1080x680")
        root.minsize(880, 550)
        self.chemistry = tk.StringVar(value=self.model.source["chemistry"])
        self.group = tk.StringVar(value=next(iter(GROUPS)))
        self.filter = tk.StringVar()
        self.name = tk.StringVar()
        self.revision = tk.StringVar()
        self.linked = tk.BooleanVar(value=True)
        self.status = tk.StringVar(value="双击数值编辑。保存前可预览源码差异。")

        toolbar = ttk.Frame(root, padding=8)
        toolbar.pack(fill="x")
        for label, variable, width in (("客户/方案", self.name, 18), ("版本", self.revision, 10)):
            ttk.Label(toolbar, text=label).pack(side="left", padx=(0, 5))
            ttk.Entry(toolbar, width=width, textvariable=variable).pack(side="left", padx=(0, 10))
        ttk.Button(toolbar, text="载入客户方案", command=self.import_scheme).pack(side="left", padx=4)
        ttk.Button(toolbar, text="另存客户方案", command=self.export_scheme).pack(side="left", padx=4)

        self.tabs = ttk.Notebook(root)
        self.tabs.pack(fill="both", expand=True, padx=8)
        self.parameter_tab = ttk.Frame(self.tabs)
        register_tab = ttk.Frame(self.tabs)
        assembly_tab = ttk.Frame(self.tabs, padding=12)
        for pane, name in ((self.parameter_tab, "保护与业务参数"), (register_tab, "DVC1124-2 寄存器"),
                           (assembly_tab, "装配与分流器")):
            self.tabs.add(pane, text=name)
        controls = ttk.Frame(self.parameter_tab, padding=5)
        controls.pack(fill="x")
        for title, variable, values in (("类别", self.group, tuple(GROUPS)), ("查看数值分支", self.chemistry, ("lfp", "nmc"))):
            ttk.Label(controls, text=title).pack(side="left", padx=5)
            ttk.Combobox(controls, textvariable=variable, values=values, state="readonly", width=17).pack(side="left")
        ttk.Label(controls, text="搜索参数 / 宏").pack(side="left", padx=(16, 5))
        ttk.Entry(controls, textvariable=self.filter).pack(side="left")
        self.parameters = table(self.parameter_tab, (("参数", 265), ("值", 85), ("单位", 58), ("源码宏", 400)))
        self.parameters.bind("<Double-1>", self.edit_parameter)

        split = ttk.Panedwindow(register_tab, orient="horizontal")
        split.pack(fill="both", expand=True)
        left, right = ttk.Frame(split), ttk.Frame(split)
        split.add(left, weight=1)
        split.add(right, weight=1)
        self.register_table = table(left, (("地址", 70), ("寄存器", 180), ("复位", 62), ("源码预测", 82)))
        self.register_table.bind("<<TreeviewSelect>>", self.show_fields)
        self.fields = table(right, (("位字段", 126), ("掩码", 68), ("读写", 70), ("编码", 70)))
        self.fields.bind("<<TreeviewSelect>>", self.field_help)
        self.fields.bind("<Double-1>", self.edit_field)
        self.description = tk.Text(right, height=8, wrap="word", state="disabled")
        self.description.pack(fill="x", padx=5, pady=6)
        reg_buttons = ttk.Frame(right, padding=4)
        reg_buttons.pack(fill="x")
        ttk.Button(reg_buttons, text="原厂 PDF", command=self.open_pdf).pack(side="left")
        ttk.Button(reg_buttons, text="跳转源码参数", command=self.goto_parameter).pack(side="left", padx=5)

        self.assembly_values = {key: tk.StringVar() for key in (*ASSEMBLY_KEYS[1:], "element", "parallel")}
        for index, (key, text) in enumerate((
                ("cell_count", "串数"), ("chemistry", "编译化学体系"), ("capacity_0p1ah", "额定容量 (Ah)"),
                ("element", "单只电阻 (µΩ)"), ("parallel", "并联只数"))):
            ttk.Label(assembly_tab, text=text).grid(row=index, column=0, sticky="w", pady=5)
            widget = (ttk.Combobox(assembly_tab, textvariable=self.assembly_values[key], values=("lfp", "nmc"),
                                   state="readonly", width=15) if key == "chemistry"
                      else ttk.Entry(assembly_tab, textvariable=self.assembly_values[key], width=17))
            widget.grid(row=index, column=1, sticky="w", padx=12)
        ttk.Label(assembly_tab, text="分流器只写回等效整数 µΩ，客户方案保留单只电阻和并联只数。").grid(
            row=5, column=0, columnspan=2, sticky="w", pady=8)

        footer = ttk.Frame(root, padding=8)
        footer.pack(fill="x")
        ttk.Checkbutton(footer, text="保存时联动软件保护 / AFE / 业务更新组", variable=self.linked).pack(side="left")
        ttk.Button(footer, text="预览并保存到源码", command=self.preview).pack(side="right")
        ttk.Label(root, textvariable=self.status, padding=(8, 0, 8, 8), wraplength=970).pack(fill="x")
        for variable in (self.chemistry, self.group, self.filter):
            variable.trace_add("write", lambda *_args: self.refresh_parameters())
        self.chemistry.trace_add("write", lambda *_args: self.refresh_registers())
        self.load_widgets()

    def load_widgets(self) -> None:
        self.name.set(self.model.name)
        self.revision.set(self.model.revision)
        values = dict(self.model.assembly, element=self.model.element, parallel=self.model.parallel)
        values["capacity_0p1ah"] = f"{values['capacity_0p1ah'] / 10:.1f}"
        for key, variable in self.assembly_values.items():
            variable.set(values[key])
        self.refresh_parameters()
        self.refresh_registers()

    def collect(self) -> None:
        self.model.name = self.name.get().strip()
        self.model.revision = self.revision.get().strip()
        values = {key: value.get().strip() for key, value in self.assembly_values.items()}
        if not re.fullmatch(r"[0-9]{1,3}(?:\.[0-9])?", values["capacity_0p1ah"]):
            raise ValueError("额定容量须用 Ah 表示，最多一位小数。")
        self.model.assembly = {"afe_model": "dvc1124", "cell_count": int(values["cell_count"]),
                               "chemistry": values["chemistry"],
                               "capacity_0p1ah": int(Decimal(values["capacity_0p1ah"]) * 10)}
        self.model.element = int(values["element"])
        self.model.parallel = int(values["parallel"])

    def refresh_parameters(self) -> None:
        self.parameters.delete(*self.parameters.get_children())
        needle = self.filter.get().strip().upper()
        for name, parameter in self.model.parameters.items():
            label = parameter_label(name)
            if self.group.get() == parameter.category and (needle in name or needle in label.upper()):
                value = display(name, self.model.get(name, self.chemistry.get()))
                self.parameters.insert("", "end", iid=name, values=(label, value, units(name)[2], name))

    def edit_parameter(self, event) -> None:
        name = self.parameters.identify_row(event.y)
        if not name:
            return
        chemistry = self.chemistry.get()
        factor, offset, unit = units(name)
        text = simpledialog.askstring("编辑源码参数", f"{name}  {unit}",
                                      initialvalue=display(name, self.model.get(name, chemistry)), parent=self.root)
        if text is None:
            return
        try:
            value = (Fraction(text.strip()) - offset) / factor
            if value.denominator != 1:
                raise ValueError("输入精度超出该参数的源码单位。")
            self.model.set(name, chemistry, int(value))
            self.refresh_parameters()
            self.refresh_registers()
        except (ValueError, ZeroDivisionError) as error:
            messagebox.showerror("参数无效", str(error), parent=self.root)

    def protection_codes(self) -> dict:
        self.model.element = int(self.assembly_values["element"].get())
        self.model.parallel = int(self.assembly_values["parallel"].get())
        chemistry = self.chemistry.get()
        return dvc_source_fields.register_values(
            lambda name: self.model.get("D008_AFE_" + name, chemistry), self.model.shunt())

    def refresh_registers(self) -> None:
        selection = self.register_table.selection()
        self.register_table.delete(*self.register_table.get_children())
        try:
            codes = self.protection_codes()
        except ValueError:
            codes = {}
        for key, reg in self.registers.items():
            value = codes.get(reg["address_decimal"])
            self.register_table.insert("", "end", iid=key, values=(
                key, reg["name"], reg.get("reset") or "—", f"0x{value:02X}" if value is not None else "—"))
        if selection:
            self.register_table.selection_set(selection[0])
            self.register_table.see(selection[0])
        else:
            self.register_table.selection_set(next(iter(self.registers)))
        self.show_fields()

    def selected_register(self):
        selected = self.register_table.selection()
        return self.registers[selected[0]] if selected else None

    def show_fields(self, _event=None) -> None:
        self.fields.delete(*self.fields.get_children())
        reg = self.selected_register()
        if not reg:
            return
        addr = reg["address_decimal"]
        code = self.register_table.item(reg["address"], "values")[3]
        for index, field in enumerate(reg["bitfields"]):
            mask = int(field["mask"], 16)
            shift = (mask & -mask).bit_length() - 1
            actual = (f"0x{((int(code, 16) & mask) >> shift):X}"
                      if code != "—" and (addr, mask) in dvc_source_fields.FIELD_OWNERS else "—")
            self.fields.insert("", "end", iid=str(index), values=(
                field["name"], field["mask"], field["access"], actual))
        self.field_help()

    def selected_field(self):
        reg, selected = self.selected_register(), self.fields.selection()
        if reg and selected:
            return reg, reg["bitfields"][int(selected[0])]
        return reg, None

    def field_help(self, _event=None) -> None:
        reg, field = self.selected_field()
        if not reg:
            return
        lines = [f"DVC1124-2 RM V1.2，第 {reg['source']['pdf_page']} 页"]
        if field:
            for key in field.get("semantic_keys", []):
                data = self.semantics.get(key, {})
                if data.get("description_zh"):
                    lines.append(data["description_zh"])
                lines.extend(f"{entry['code_text']}  {entry['meaning_zh']}" for entry in data.get("encodings", [])
                             if "code_text" in entry and "meaning_zh" in entry)
            name = dvc_source_fields.FIELD_OWNERS.get((reg["address_decimal"], int(field["mask"], 16)))
            lines.append("参数所有者 D008_AFE_" + name if name else "此字段供资料查阅。")
        else:
            lines.append(reg["name"])
        self.description.configure(state="normal")
        self.description.delete("1.0", "end")
        self.description.insert("1.0", "\n".join(lines))
        self.description.configure(state="disabled")

    def edit_field(self, _event=None) -> None:
        reg, field = self.selected_field()
        if not field:
            return
        addr, mask = reg["address_decimal"], int(field["mask"], 16)
        if (addr, mask) not in dvc_source_fields.FIELD_OWNERS:
            return
        code = self.fields.item(self.fields.selection()[0], "values")[3]
        text = simpledialog.askstring("保护位编码", f"{reg['address']}  {field['name']}  十进制或 0x 十六进制",
                                      initialvalue=code if code != "—" else "", parent=self.root)
        if text is None:
            return
        try:
            self.protection_codes()
            chemistry = self.chemistry.get()
            changes = dvc_source_fields.edit_field(addr, mask, int(text.strip(), 0),
                lambda name: self.model.get("D008_AFE_" + name, chemistry), self.model.shunt())
            for name, value in changes.items():
                self.model.set(name, chemistry, value)
            self.refresh_parameters()
            self.refresh_registers()
        except ValueError as error:
            messagebox.showerror("编码无效", str(error), parent=self.root)

    def goto_parameter(self) -> None:
        reg, field = self.selected_field()
        if not field:
            return
        name = dvc_source_fields.FIELD_OWNERS.get((reg["address_decimal"], int(field["mask"], 16)))
        if name:
            self.tabs.select(self.parameter_tab)
            self.group.set("AFE 硬件保护")
            self.filter.set("D008_AFE_" + name)

    def open_pdf(self) -> None:
        reg = self.selected_register()
        if reg:
            source = reg["source"]
            pdf = self.directory / REFERENCE / "sources" / source["filename"]
            webbrowser.open(pdf.as_uri() + f"#page={source['pdf_page']}")

    def import_scheme(self) -> None:
        path = filedialog.askopenfilename(parent=self.root, filetypes=[("D008 客户方案", "*.json")])
        if path:
            try:
                model = SourceDefaults(self.directory)
                skipped = model.load_scheme(json.loads(Path(path).read_text(encoding="utf-8-sig")))
                self.model = model
                self.load_widgets()
                self.status.set("方案已载入窗口，请预览后保存。" +
                                (" 本分支已移除的字段 " + "、".join(skipped) if skipped else ""))
            except (OSError, ValueError, AttributeError, KeyError, TypeError) as error:
                messagebox.showerror("方案载入失败", str(error), parent=self.root)

    def export_scheme(self) -> None:
        try:
            self.collect()
            scheme = self.model.scheme()
            path = filedialog.asksaveasfilename(parent=self.root, defaultextension=".json",
                                                filetypes=[("D008 客户方案", "*.json")])
            if path:
                Path(path).write_text(json.dumps(scheme, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
                self.status.set("客户方案已保存。")
        except (OSError, ValueError) as error:
            messagebox.showerror("方案保存失败", str(error), parent=self.root)

    def preview(self) -> None:
        try:
            self.collect()
            config, edited = self.model.changes(self.linked.get())
        except (OSError, ValueError) as error:
            messagebox.showerror("参数无效", str(error), parent=self.root)
            return
        summary = [f"装配 {config['cell_count']}S {config['chemistry'].upper()}  ·  {config['capacity_0p1ah'] / 10:.1f} Ah",
                   "保存后的更新组 " + ("、".join(config["update_groups"]) or "保参"),
                   "休眠参数用于新建默认配置。旧板换装配时需使用出厂包。",
                   ""]
        for file, (_, text) in edited.items():
            summary.extend(difflib.unified_diff(self.model.text(file).splitlines(), text.splitlines(),
                                                fromfile=file, tofile=file, lineterm=""))
        dialog = tk.Toplevel(self.root)
        dialog.title("D008 源码差异")
        dialog.geometry("960x540")
        dialog.transient(self.root)
        dialog.grab_set()
        diff = scrolledtext.ScrolledText(dialog, wrap="none")
        diff.pack(fill="both", expand=True, padx=8, pady=8)
        diff.insert("1.0", "\n".join(summary))
        diff.configure(state="disabled")

        def save() -> None:
            try:
                if build_options.read_source(self.directory, PRODUCT, for_edit=True) != self.model.source:
                    raise ValueError("装配或 OTA 策略已变化，请重新载入参数窗口。")
                build_options.save_source(self.directory, PRODUCT, config, source_defaults=edited)
                self.model = SourceDefaults(self.directory)
                self.load_widgets()
                self.status.set("已保存源码。")
                dialog.destroy()
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                messagebox.showerror("保存失败", str(error), parent=dialog)

        commands = ttk.Frame(dialog, padding=8)
        commands.pack(fill="x")
        ttk.Button(commands, text="确认保存", command=save).pack(side="right", padx=4)
        ttk.Button(commands, text="返回编辑", command=dialog.destroy).pack(side="right", padx=4)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", choices=(PRODUCT,), default=PRODUCT)
    parser.parse_args()
    root = tk.Tk()
    try:
        Window(root, ROOT)
    except (OSError, ValueError) as error:
        messagebox.showerror("读取 D008 源码失败", str(error), parent=root)
        root.destroy()
        return 1
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

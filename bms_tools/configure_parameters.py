"""源码参数与原厂 AFE 位查看器；仅编辑源码，不编译、不连接设备。"""
from __future__ import annotations

import ast
import difflib
import json
import os
import re
from fractions import Fraction
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

import build_options
from afe_register_catalog import REFERENCE

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


def unit(name: str) -> str:
    name = name.removesuffix("_FOR")
    for suffix, text in (("_10MS", "10 ms"), ("_A10", "0.1 A"), ("_X10", "(℃+40)×10"),
                         ("_UOHM", "µΩ"), ("_MV", "mV"), ("_MS", "ms"), ("_US", "µs"),
                         ("_MA", "mA"), ("_0P1AH", "0.1 Ah")):
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
        self.product, self.chemistry = product, chemistry
        self.folder = ROOT / "bms/products" / product
        self.settings = build_options.read_source(ROOT, product, for_edit=True)
        self.paths = [self.folder / "bms_product.h", self.folder / f"{product}_protection_config.h",
                      self.folder / "afe_register_config.h"]
        self.paths.append(self.folder / "dvc1124_product_defaults.h" if product == "d008"
                          else ROOT / build_options.SHARED_PARAMETERS)
        self.originals = {path: path.read_bytes() for path in self.paths}
        self.rows = []
        for path, raw in self.originals.items():
            text = raw.decode("utf-8-sig")
            for match in DEFINE.finditer(text):
                name, argument, expression = match.groups()
                if not name.startswith(("D008_SW_", "D008_AFE_", "BMS_SW_", "SH3673510_HW_DEFAULT_",
                                        "BMS_SLEEP_", "BMS_BALANCE_", "BMS_HEATER_")):
                    continue
                expression = expression.split("/*", 1)[0].strip()
                try:
                    ternary = TERNARY.fullmatch(expression) if argument else None
                    value = integer(ternary.group(1 if chemistry == "lfp" else 2)) if ternary else integer(expression)
                except (ValueError, SyntaxError, ZeroDivisionError):
                    continue
                self.rows.append(dict(path=path, name=name, argument=argument or "", value=value,
                    ternary=bool(ternary), edit=str(value), scope="D011/D013/D014 共用" if path.name.startswith("sh367") else product.upper()))
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
        for address, mask, value in re.findall(r"X\((0x[0-9A-Fa-f]+)u?,\s*(0x[0-9A-Fa-f]+)u?,\s*(0x[0-9A-Fa-f]+)u?\)", source):
            self.presets[int(address, 16)] = (int(mask, 16), int(value, 16))

    def replacements(self, element: int, parallel: int, debug: bool) -> dict:
        if not 1 <= element <= 60000 or not 1 <= parallel <= 16:
            raise ValueError("单只/分子为 1..60000 µΩ，并联只数/分母为 1..16")
        edited = {p: b.decode("utf-8-sig") for p, b in self.originals.items()}
        for row in self.rows:
            value = integer(row["edit"])
            maximum = 86400 * 365 if row["name"].endswith("_SECONDS") else 65535
            if not 0 <= value <= maximum:
                raise ValueError(f"{row['name']} 超出允许范围 0..{maximum}")
            if value == row["value"]: continue
            pattern = re.compile(r"(?m)^(#define[ \t]+" + re.escape(row["name"] + row["argument"]) + r"[ \t]+)([^\r\n]+)")
            def replace(match):
                body = match[2]
                if row["ternary"]:
                    number = 1 if self.chemistry == "lfp" else 2
                    found = TERNARY.search(body)
                    if not found: raise ValueError("参数表达式已变化")
                    start, end = found.span(number)
                    body = body[:start] + str(value) + "u" + body[end:]
                else:
                    comment = " /*" + body.split("/*", 1)[1] if "/*" in body else ""
                    body = str(value) + "u" + comment
                return match[1] + body
            edited[row["path"]], count = pattern.subn(replace, edited[row["path"]])
            if count != 1: raise ValueError("参数定义不唯一：" + row["name"])
        board_path = self.folder / "bms_product.h"
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
            config += f" \\\n    X(0x{address:02X}u, 0x{mask:02X}u, 0x{value:02X}u)"
        edited[path] = config + "\n"
        return {p: text for p, text in edited.items() if text != self.originals[p].decode("utf-8-sig")}

    def save(self, edited: dict) -> None:
        # 同时保留摘要更新涉及的产品文件；保存失败按原字节回滚，不碰其它脏文件。
        paths = set(edited) | {ROOT / f"bms/products/{p}/{name}" for p in build_options.PRODUCTS
                              for name in ("bms_product.h", f"{p}_protection_config.h")}
        paths.add(ROOT / "bms/products/d008/d008_product_profile.h")
        originals = {p: p.read_bytes() for p in paths}
        for p in edited:
            if p.read_bytes() != self.originals[p]: raise ValueError("源码已被其它程序修改，请重新载入：" + str(p))
        written = {}
        try:
            for p, text in edited.items():
                raw = (b"\xef\xbb\xbf" if originals[p].startswith(b"\xef\xbb\xbf") else b"") + text.encode("utf-8")
                p.write_bytes(raw); written[p] = raw
            affected = ("d011", "d013", "d014") if ROOT / build_options.SHARED_PARAMETERS in edited else (self.product,)
            for product in affected:
                result = build_options.save_source(ROOT, product, {})
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
        self.root.geometry("1260x820")
        self.product = tk.StringVar(value=product)
        initial = build_options.read_source(ROOT, product, for_edit=True)
        self.chemistry = tk.StringVar(value=initial["chemistry"])
        self.search = tk.StringVar(); self.element = tk.StringVar(); self.parallel = tk.StringVar(); self.debug = tk.BooleanVar()
        self.config = None; self.row_edits = {}; self.register_edits = {}
        top = ttk.Frame(self.root, padding=8); top.pack(fill="x")
        for name, variable, values in (("产品", self.product, build_options.PRODUCTS), ("参数表类型", self.chemistry, ("lfp", "nmc"))):
            ttk.Label(top, text=name).pack(side="left", padx=4)
            box = ttk.Combobox(top, textvariable=variable, values=values, state="readonly", width=12)
            box.pack(side="left"); box.bind("<<ComboboxSelected>>", self.reload)
        ttk.Button(top, text="重新载入", command=self.reload).pack(side="left", padx=8)
        ttk.Button(top, text="预览 Git 差异 / 保存源码", command=self.save).pack(side="right")
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
        self.table = self.tree(self.parameters, ("名称", "当前源码", "修改值", "单位", "影响范围", "宏"), (250,90,90,125,170,330))
        self.table.bind("<Double-1>", self.edit_parameter)
        bar = ttk.Frame(self.registers); bar.pack(fill="x")
        ttk.Label(bar, text="搜索地址 / 寄存器名 / 位名").pack(side="left")
        ttk.Entry(bar, textvariable=self.search, width=40).pack(side="left", padx=8)
        ttk.Button(bar, text="打开对应原厂 PDF", command=self.open_pdf).pack(side="right")
        self.search.trace_add("write", lambda *_: self.load_registers())
        self.regtable = self.tree(self.registers, ("地址", "寄存器", "复位值", "源码预设", "可写掩码", "原页"), (70,380,90,140,100,200), height=12)
        self.regtable.bind("<<TreeviewSelect>>", self.show_bits)
        self.bits = self.tree(self.registers, ("字段", "位掩码", "属性", "源码编码", "含义 / 编码"), (150,80,130,100,700), height=9)
        self.bits.bind("<Double-1>", self.edit_bit)
        ttk.Label(self.registers, text="双击可编辑的固定字段设置源码预设；双击已有预设可修改。保护字段在参数页编辑；原始编码可导出供内部版临时调试。", wraplength=1200).pack(fill="x")
        ttk.Button(self.registers, text="为选中字段生成内部版调试文件", command=self.export_debug).pack(anchor="e", pady=5)
        ttk.Button(self.registers, text="跳到此寄存器的硬件保护参数", command=self.goto_parameter).pack(anchor="e", pady=3)
        ttk.Button(self.registers, text="取消此寄存器的源码覆盖", command=self.remove_preset).pack(anchor="e", pady=3)
        info = ttk.Frame(self.board, padding=18); info.pack(fill="x")
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
        return self.config and (any(r["edit"] != str(r["value"]) for r in self.config.rows) or self.register_edits or self.element.get()!=str(self.config.element) or self.parallel.get()!=str(self.config.parallel) or self.debug.get()!=self.config.debug)

    def reload(self, event=None):
        if self.has_edits() and not messagebox.askyesno("未保存修改", "放弃本地修改并重新载入？"):
            self.product.set(self.config.product); self.chemistry.set(self.config.chemistry); return
        try:
            if self.config and self.product.get() != self.config.product:
                self.chemistry.set(build_options.read_source(ROOT, self.product.get(), for_edit=True)["chemistry"])
            self.config = Configuration(self.product.get(),self.chemistry.get());self.register_edits={}
            self.element.set(str(self.config.element));self.parallel.set(str(self.config.parallel));self.debug.set(self.config.debug)
            self.table.delete(*self.table.get_children())
            for index,row in enumerate(self.config.rows): self.table.insert("","end",iid=str(index),values=(label(row["name"]),row["value"],row["edit"],unit(row["name"]),row["scope"],row["name"]))
            model=self.config.settings["afe_model"]
            self.status.config(text=f"{self.config.product.upper()} · {model} · 参数表 {self.config.chemistry.upper()} · {self.config.family['revision']} · 目录 {CATALOG['catalog_id']} · 表内数值为协议编码单位")
            self.load_registers()
        except Exception as error: messagebox.showerror("载入失败",str(error))

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
        value=askstring(label(row["name"]),f"单位：{unit(row['name'])}；范围与恢复关系由固件继续校验。\n影响：{row['scope']}\n{row['name']}",initialvalue=row["edit"],parent=self.root)
        if value is not None:
            try:row["edit"]=str(integer(value));values=list(self.table.item(item,"values"));values[2]=row["edit"];self.table.item(item,values=values)
            except Exception as error:messagebox.showerror("无效值",str(error))

    def load_registers(self):
        if not self.config:return
        selected=self.regtable.selection(); self.regtable.delete(*self.regtable.get_children()); self.bits.delete(*self.bits.get_children())
        query=self.search.get().lower()
        for register in self.config.family["registers"]:
            text=json.dumps(register,ensure_ascii=False).lower()
            if query and query not in text and query not in f"0x{register['address']:02x}":continue
            address=register["address"]; preset=self.config.presets.get(address)
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
        for index,field in enumerate(register["fields"]):
            value=str((preset[1]&field["mask"])>>field["shift"]) if preset[0]&field["mask"]==field["mask"] else "未覆盖"
            description=field["description"]+" "+json.dumps(field["encoding"],ensure_ascii=False)
            self.bits.insert("","end",iid=str(index),values=(field["name"],f"0x{field['mask']:02X}","保留" if field["reserved"] else field["access"],value,description))

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
            register,field,value=self.ask_field(source=True)
            if value is None:return
            address=register["address"];mask,previous=self.config.presets.get(address,(0,0))
            self.config.presets[address]=(mask|field["mask"],(previous&~field["mask"])|(value<<field["shift"]))
            self.register_edits[address]=True;self.load_registers();self.regtable.selection_set(str(address));self.show_bits()
        except ValueError as error:messagebox.showinfo("字段所有权",str(error))

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
            edited=self.config.replacements(integer(self.element.get()),integer(self.parallel.get()),self.debug.get())
            if not edited:messagebox.showinfo("源码配置","没有修改");return
            preview=tk.Toplevel(self.root);preview.title("保存前差异（不编译、不 OTA）");preview.geometry("1100x650")
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

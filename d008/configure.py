"""VS Code 的 D008 正式固件配置窗口。"""
import argparse
from decimal import Decimal, InvalidOperation
from pathlib import Path
import subprocess
import sys
import tkinter as tk
from tkinter import messagebox, ttk

APP = Path(__file__).resolve().parent
ROOT = APP.parent
CHEMISTRIES = {"铁锂 (LFP)": "lfp", "三元 (NMC)": "nmc"}
PROTOCOLS = {"锂神扩展": "lishen-extended", "锂神标准": "lishen", "A1.3.3": "a133"}


def build_args(cells_text, chemistry, capacity_text, protocol):
    try:
        cells = int(cells_text)
    except ValueError as exc:
        raise ValueError("串数应为整数。") from exc
    max_cells = 24 if chemistry == "lfp" else 23
    if not 4 <= cells <= max_cells:
        raise ValueError(f"串数应为 4～{max_cells}。")
    try:
        capacity = Decimal(capacity_text)
        if not capacity.is_finite() or not Decimal("0.1") <= capacity <= 1000:
            raise ValueError("容量应为 0.1～1000.0 Ah。")
        capacity_dah = capacity * 10
        if capacity_dah != capacity_dah.to_integral_value():
            raise ValueError("容量最多支持一位小数。")
    except InvalidOperation as exc:
        raise ValueError("请输入有效的额定容量。") from exc
    return ["--profile", "custom", "--cells", str(cells), "--chemistry", chemistry,
            "--capacity-dah", str(int(capacity_dah)), "--sif", protocol]


def choose_build(editor=None):
    window = tk.Tk()
    window.title("D008 正式固件配置")
    window.resizable(False, False)
    panel = ttk.Frame(window, padding=16)
    panel.grid()
    choices = {}
    cells = tk.StringVar(value="16")
    chemistry = tk.StringVar(value=next(iter(CHEMISTRIES)))
    capacity = tk.StringVar(value="20.0")
    protocol = tk.StringVar(value=next(iter(PROTOCOLS)))

    def start(image, android_ota=False):
        try:
            selected = build_args(cells.get(), CHEMISTRIES[chemistry.get()], capacity.get(),
                                  PROTOCOLS[protocol.get()])
        except ValueError as exc:
            messagebox.showerror("配置无效", str(exc), parent=window)
            return
        if image:
            selected += ["--image-dir", str(ROOT / "bin")]
        if android_ota:
            selected.append("--android-ota")
        choices["args"] = selected
        window.destroy()

    ttk.Label(panel, text="D008 / DVC1124-2 · 正式固件").grid(
        row=0, column=0, columnspan=2, sticky="w", pady=(0, 8))
    for row, (label, entry) in enumerate((
        ("串数", ttk.Spinbox(panel, from_=4, to=24, textvariable=cells)),
        ("电池类型", ttk.Combobox(panel, textvariable=chemistry, values=tuple(CHEMISTRIES), state="readonly")),
        ("额定容量 (Ah)", ttk.Spinbox(panel, from_=0.1, to=1000, increment=0.1,
                                     format="%.1f", textvariable=capacity)),
        ("一线通协议", ttk.Combobox(panel, textvariable=protocol, values=tuple(PROTOCOLS), state="readonly")),
    ), start=1):
        ttk.Label(panel, text=label).grid(row=row, column=0, sticky="w", padx=(0, 14), pady=4)
        entry.grid(row=row, column=1, sticky="ew")

    def open_protection_defaults():
        try:
            subprocess.Popen([editor, "--reuse-window", str(APP / "product.c")], cwd=ROOT)
        except OSError as exc:
            messagebox.showerror("打开保护参数源码失败", str(exc), parent=window)

    if editor:
        ttk.Button(panel, text="打开保护参数源码", command=open_protection_defaults).grid(
            row=5, column=1, sticky="e", pady=(6, 0))
    ttk.Label(panel, text="容量用于首次建档。普通 OTA 保留已有参数。\n"
                          "编译并 OTA 前，请用安卓 App 连接装配一致的 D008。").grid(
        row=6, column=0, columnspan=2, sticky="w", pady=(10, 0))
    buttons = ttk.Frame(panel)
    buttons.grid(row=7, column=0, columnspan=2, sticky="e", pady=(12, 0))
    for label, action in (("仅链接 ELF", lambda: start(False)), ("编译 BIN", lambda: start(True)),
                          ("编译并 OTA", lambda: start(True, android_ota=True)), ("取消", window.destroy)):
        ttk.Button(buttons, text=label, command=action).pack(side="left", padx=3)
    window.bind("<Escape>", lambda _: window.destroy())
    window.mainloop()
    return choices.get("args")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", help="VS Code 可执行文件")
    args = choose_build(parser.parse_args().editor)
    if args is None:
        return 0
    return subprocess.run([sys.executable, "-X", "utf8", "-B", str(APP / "build.py"), *args],
                          cwd=ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())

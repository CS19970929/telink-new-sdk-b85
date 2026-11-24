"""D008 源码装配及 OTA 参数配置窗口。"""
from decimal import Decimal
from pathlib import Path
import re
import subprocess
import sys
import tkinter as tk
from tkinter import messagebox, ttk

import build_options


ROOT = Path(__file__).resolve().parents[1]
PRODUCT = "d008"
UPDATE_LABELS = {
    "sw": "软件保护",
    "afe": "AFE 硬件保护",
    "business": "容量、加热和均衡",
    "soc": "SOC 满空电压",
    "soc_state": "清空 SOC 和循环",
    "factory_reset": "恢复出厂，清空 BMS 业务数据",
}


def main() -> int:
    window = tk.Tk()
    window.title("D008 源码配置")
    window.resizable(False, False)
    try:
        source = build_options.read_source(ROOT, PRODUCT, for_edit=True)
    except (OSError, ValueError) as error:
        messagebox.showerror("读取源码配置失败", str(error), parent=window)
        window.destroy()
        return 1

    cells = tk.StringVar(value=str(source["cell_count"]))
    chemistry = tk.StringVar(value=source["chemistry"])
    capacity = tk.StringVar(value=f"{source['capacity_0p1ah'] / 10:.1f}")
    new_package = tk.BooleanVar(value=False)
    profile = build_options.source_profile(ROOT)
    update_id = source.get("update_id")
    status = tk.StringVar(value=f"当前装配 {profile}" + (f" · 参数包 {update_id}" if update_id else ""))
    groups = {
        group: tk.BooleanVar(value=group in source.get("update_groups", []))
        for group in UPDATE_LABELS
    }
    link_profile = None

    def choose_group(group: str) -> None:
        if not groups[group].get():
            return
        if group == "factory_reset":
            for name in groups:
                if name != group:
                    groups[name].set(False)
        else:
            groups["factory_reset"].set(False)

    def save(link: bool = False) -> None:
        nonlocal link_profile
        try:
            cell_text = cells.get().strip()
            if not re.fullmatch(r"[0-9]{1,2}", cell_text):
                raise ValueError("串数须为整数。")
            capacity_text = capacity.get().strip()
            if not re.fullmatch(r"[0-9]{1,3}(?:\.[0-9])?", capacity_text):
                raise ValueError("额定容量须在 0.1～655.3 Ah，最多一位小数。")
            selected = [group for group, active in groups.items() if active.get()]
            saved = build_options.save_source(ROOT, PRODUCT, {
                "cell_count": int(cell_text),
                "chemistry": chemistry.get(),
                "capacity_0p1ah": int(Decimal(capacity_text) * 10),
                "update_groups": selected,
            }, new_package=new_package.get())
            profile = build_options.source_profile(ROOT)
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            messagebox.showerror("保存失败", str(error), parent=window)
            return
        new_package.set(False)
        update_id = saved["configuration"].get("update_id")
        status.set(f"已保存 {profile}" + (f" · 参数包 {update_id}" if update_id else " · 普通保参"))
        if link:
            link_profile = profile
            window.quit()

    box = ttk.Frame(window, padding=16)
    box.grid()
    ttk.Label(box, text="D008 · DVC1124").grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 10))
    ttk.Label(box, text="串数").grid(row=1, column=0, sticky="w")
    ttk.Spinbox(box, from_=4, to=24, textvariable=cells, width=8).grid(row=1, column=1, sticky="w")
    ttk.Label(box, text="电芯化学体系").grid(row=2, column=0, sticky="w", pady=5)
    ttk.Combobox(box, textvariable=chemistry, values=("lfp", "nmc"), state="readonly", width=8).grid(
        row=2, column=1, sticky="w")
    ttk.Label(box, text="额定容量 (Ah)").grid(row=3, column=0, sticky="w")
    ttk.Entry(box, textvariable=capacity, width=10).grid(row=3, column=1, sticky="w")

    updates = ttk.LabelFrame(box, text="固件首次启动时应用的更新", padding=8)
    updates.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(12, 8))
    for group, label in UPDATE_LABELS.items():
        ttk.Checkbutton(updates, text=label, variable=groups[group],
                        command=lambda current=group: choose_group(current)).pack(anchor="w", pady=2)
    ttk.Checkbutton(updates, text="生成新的参数包标识", variable=new_package).pack(anchor="w", pady=(6, 0))
    ttk.Label(box, textvariable=status, wraplength=420).grid(row=5, column=0, columnspan=2, sticky="w")

    buttons = ttk.Frame(box)
    buttons.grid(row=6, column=0, columnspan=2, sticky="e", pady=(12, 0))
    ttk.Button(buttons, text="保存到源码", command=save).pack(side="left", padx=4)
    ttk.Button(buttons, text="保存并链接 ELF", command=lambda: save(True)).pack(side="left", padx=4)
    ttk.Button(buttons, text="关闭", command=window.quit).pack(side="left", padx=4)
    window.protocol("WM_DELETE_WINDOW", window.quit)
    window.mainloop()
    window.destroy()
    if link_profile:
        return subprocess.call([sys.executable, "-X", "utf8", "-B", str(ROOT / "bms_tools/bms.py"),
                                "--production", "--d008-profile", link_profile, "link", "--jobs", "4"],
                               cwd=ROOT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

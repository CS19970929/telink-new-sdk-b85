"""D008 源码配置、链接、编译和 Android OTA 窗口。"""
from decimal import Decimal
from hashlib import sha256
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import tkinter as tk
from tkinter import messagebox, ttk

import build_options


ROOT = Path(__file__).resolve().parents[1]
PRODUCT = "d008"
MODES = {"开发": "development", "生产": "production"}
CHEMISTRIES = {"磷酸铁锂": "lfp", "三元锂（4.20 V）": "nmc"}
UPDATE_LABELS = {
    "sw": "软件保护",
    "afe": "AFE 硬件保护",
    "business": "容量 / 加热 / 均衡",
    "soc": "SOC 满空电压",
    "soc_state": "更换电池：重置 SOC / 循环",
    "factory_reset": "恢复出厂：清空 BMS 业务数据",
}


def settings_file(root: Path) -> Path:
    key = sha256(str(root).encode("utf-8")).hexdigest()[:12].upper()
    base = Path(os.environ.get("LOCALAPPDATA", tempfile.gettempdir()))
    return base / "BmsBuildMenu" / f"{key}.json"


def saved_mode(path: Path) -> str:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return "生产"
    previous = value.get("Mode") if isinstance(value, dict) else None
    return next((label for label, mode in MODES.items() if mode == previous), "生产")


def main() -> int:
    window = tk.Tk()
    window.title("BMS 源码配置与编译 · D008")
    window.resizable(False, False)
    cells = tk.StringVar()
    chemistry = tk.StringVar()
    capacity = tk.StringVar()
    preferences = settings_file(ROOT)
    mode = tk.StringVar(value=saved_mode(preferences))
    clean = tk.BooleanVar()
    send = tk.BooleanVar()
    new_package = tk.BooleanVar()
    groups = {group: tk.BooleanVar() for group in UPDATE_LABELS}
    identity = tk.StringVar()
    status = tk.StringVar()
    cell_range = tk.StringVar()
    task = None
    loading = False

    def reload_source() -> None:
        nonlocal loading, source
        source = build_options.read_source(ROOT, PRODUCT, for_edit=True)
        loading = True
        try:
            cells.set(str(source["cell_count"]))
            chemistry.set(next(label for label, name in CHEMISTRIES.items() if name == source["chemistry"]))
            capacity.set(f"{source['capacity_0p1ah'] / 10:.1f}")
            for name, enabled in groups.items():
                enabled.set(name in source.get("update_groups", []))
            new_package.set(False)
            profile = build_options.source_profile(ROOT)
            update_id = source.get("update_id")
            identity.set(f"{profile} · 参数包 {update_id}" if update_id else f"{profile} · 源码保参")
        finally:
            loading = False

    def select_group(group: str) -> None:
        if not groups[group].get():
            return
        if group == "factory_reset":
            for name in groups:
                if name != group:
                    groups[name].set(False)
        else:
            groups["factory_reset"].set(False)

    def input_values() -> dict:
        cell_text = cells.get().strip()
        capacity_text = capacity.get().strip()
        if not re.fullmatch(r"[0-9]{1,2}", cell_text):
            raise ValueError("串数须为整数。")
        if not re.fullmatch(r"[0-9]{1,3}(?:\.[0-9])?", capacity_text):
            raise ValueError("额定容量须在 0.1～655.3 Ah，最多一位小数。")
        return {"cell_count": int(cell_text), "chemistry": CHEMISTRIES[chemistry.get()],
                "capacity_0p1ah": int(Decimal(capacity_text) * 10),
                "update_groups": [group for group, enabled in groups.items() if enabled.get()]}

    def save() -> None:
        try:
            build_options.save_source(ROOT, PRODUCT, input_values(), new_package=new_package.get())
            reload_source()
            status.set("已保存到源码。")
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            messagebox.showerror("保存失败", str(error), parent=window)

    def check_saved(verify_package: bool) -> None:
        selected = (build_options.verify_source(ROOT, PRODUCT) if verify_package
                    else build_options.read_source(ROOT, PRODUCT, for_edit=True))
        if new_package.get() or any(selected.get(key, []) != value for key, value in input_values().items()):
            raise ValueError("当前窗口还有未保存的配置，请先保存到源码。")

    def open_parameters() -> None:
        try:
            check_saved(False)
            window.withdraw()
            try:
                script = ROOT / "bms_tools/configure_parameters.py"
                rc = subprocess.call([sys.executable, "-X", "utf8", "-B", str(script)], cwd=ROOT)
            finally:
                window.deiconify()
            if rc:
                raise ValueError("参数编辑器未正常结束，请查看终端。")
            reload_source()
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            messagebox.showerror("打开参数编辑器失败", str(error), parent=window)

    def start(image: bool) -> None:
        nonlocal task
        try:
            check_saved(True)
            selected_mode = MODES[mode.get()]
            if image:
                # 由原 Sender 工作流核对装配、签核、镜像和 manifest。
                task = ["android_ota.py", "--target", PRODUCT, "--mode", selected_mode]
                if clean.get():
                    task.append("--rebuild")
                if not send.get():
                    task.append("--build-only")
            else:
                task = ["bms.py", "--d008-profile", build_options.source_profile(ROOT)]
                if selected_mode == "production":
                    task.append("--production")
                task += ["link", "--jobs", "4"]
                if clean.get():
                    task.append("--clean")
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            messagebox.showerror("源码配置未就绪", str(error), parent=window)
            return
        window.quit()

    box = ttk.Frame(window, padding=16)
    box.grid(sticky="nsew")
    for column in (1, 3):
        box.columnconfigure(column, weight=1)
    ttk.Label(box, text="板型").grid(row=0, column=0, sticky="w")
    ttk.Label(box, text="D008").grid(row=0, column=1, sticky="w")
    ttk.Label(box, text="AFE").grid(row=0, column=2, sticky="w", padx=(16, 10))
    ttk.Label(box, text="DVC1124-2").grid(row=0, column=3, sticky="w")
    ttk.Label(box, text="构建模式").grid(row=1, column=0, sticky="w", pady=5)
    ttk.Combobox(box, textvariable=mode, values=tuple(MODES), state="readonly", width=17).grid(
        row=1, column=1, sticky="w", padx=(10, 0))
    ttk.Label(box, text="串数").grid(row=1, column=2, sticky="w", padx=(16, 10))
    cell_box = ttk.Frame(box)
    cell_box.grid(row=1, column=3, sticky="w")
    cell_input = ttk.Spinbox(cell_box, from_=4, to=24, textvariable=cells, width=7)
    cell_input.pack(side="left")
    ttk.Label(cell_box, textvariable=cell_range).pack(side="left", padx=6)
    ttk.Label(box, text="电池类型").grid(row=2, column=0, sticky="w", pady=5)
    ttk.Combobox(box, textvariable=chemistry, values=tuple(CHEMISTRIES), state="readonly", width=17).grid(
        row=2, column=1, sticky="w", padx=(10, 0))
    ttk.Button(box, text="保护参数 / AFE 位配置", command=open_parameters).grid(
        row=2, column=2, columnspan=2, sticky="ew", padx=(16, 0))
    ttk.Label(box, text="额定容量 (Ah)").grid(row=3, column=0, sticky="w", pady=5)
    ttk.Spinbox(box, from_=0.1, to=655.3, increment=0.1, format="%.1f",
                textvariable=capacity, width=17).grid(row=3, column=1, sticky="w", padx=(10, 0))

    updates = ttk.LabelFrame(box, text="保存到源码的 OTA 更新策略（全不选则保留参数）", padding=8)
    updates.grid(row=4, column=0, columnspan=4, sticky="ew", pady=(12, 6))
    for index, (group, label) in enumerate(UPDATE_LABELS.items()):
        ttk.Checkbutton(updates, text=label, variable=groups[group],
                        command=lambda name=group: select_group(name)).grid(
            row=index // 2, column=index % 2, sticky="w", padx=(0, 15), pady=3)
    ttk.Label(box, text="旧板更换串数或电芯类型时使用出厂包。出厂包首次启动清理 BMS 业务区。",
              wraplength=485).grid(row=5, column=0, columnspan=4, sticky="w", pady=5)
    for index, (label, variable) in enumerate((
            ("清理后全量重编译", clean),
            ("生成后发送安卓并请求 OTA", send),
            ("保存时制作新参数更新包", new_package)), start=6):
        ttk.Checkbutton(box, text=label, variable=variable).grid(
            row=index, column=0, columnspan=4, sticky="w", pady=2)
    ttk.Label(box, textvariable=identity).grid(row=9, column=0, columnspan=4, sticky="w", pady=6)
    ttk.Label(box, textvariable=status, wraplength=485).grid(row=10, column=0, columnspan=4, sticky="w")

    buttons = ttk.Frame(box)
    buttons.grid(row=11, column=0, columnspan=4, sticky="e", pady=(12, 0))
    for label, action in (("保存到源码", save), ("仅链接 ELF", lambda: start(False)),
                          ("按源码编译", lambda: start(True)), ("取消", window.quit)):
        ttk.Button(buttons, text=label, command=action).pack(side="left", padx=4)

    def adjust_cells(*_args) -> None:
        maximum = 23 if CHEMISTRIES.get(chemistry.get()) == "nmc" else 24
        cell_input.configure(to=maximum)
        cell_range.set(f"4～{maximum}")

    def capacity_changed(*_args) -> None:
        value = capacity.get().strip()
        if (not loading and not groups["factory_reset"].get() and
                re.fullmatch(r"[0-9]{1,3}(?:\.[0-9])?", value) and
                Decimal(value) * 10 != source["capacity_0p1ah"]):
            groups["business"].set(True)

    source = {}
    chemistry.trace_add("write", adjust_cells)
    capacity.trace_add("write", capacity_changed)
    try:
        reload_source()
    except (OSError, ValueError) as error:
        messagebox.showerror("读取源码配置失败", str(error), parent=window)
        window.destroy()
        return 1
    window.protocol("WM_DELETE_WINDOW", window.quit)
    window.mainloop()
    selected_mode = MODES[mode.get()]
    window.destroy()
    if task:
        try:
            preferences.parent.mkdir(parents=True, exist_ok=True)
            preferences.write_text(json.dumps({"Mode": selected_mode, "Target": PRODUCT}), encoding="utf-8")
        except OSError:
            pass
        script, *args = task
        return subprocess.call([sys.executable, "-X", "utf8", "-B", str(ROOT / "bms_tools" / script), *args],
                               cwd=ROOT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

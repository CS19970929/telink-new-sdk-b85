"""Runtime localization helpers for the Comm Tool BMS desktop UI."""

from __future__ import annotations

import re
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from typing import Any, Callable


LANG_ZH = "zh"
LANG_EN = "en"

_language = LANG_ZH
_patched = False


_EXACT_EN = {
    "请选择固件文件": "Select a firmware file",
    "锂神实时": "Lishen live data",
    "协议范围": "Available fields",
    "仅提供SOC、总压和电池温度": "SOC, pack voltage and battery temperature only",
    "此Comm Tool仅支持A1.3.3；锂神接收需要升级Comm Tool App": "This legacy Comm Tool supports A1.3.3 only; update its App for Lishen",
    "Comm Tool接收模式不可用，请核对PC10/11配置或升级App": "Receiver unavailable; check PC10/11 configuration or update the App",
    "自动识别": "Auto detect",
    "锂神 V1.1.1": "Lishen V1.1.1",
    "电池温度": "Battery temperature",
    "接收模式不会修改BMS协议；锂神模式仅提供SOC、总压和电池温度。": "Receiver mode does not change the BMS protocol; Lishen provides SOC, pack voltage and battery temperature only.",
    "；旧Comm Tool仅支持A1.3.3": "; this legacy Comm Tool supports A1.3.3 only",
    "接收协议不匹配，请切换模式": "Protocol mismatch; select the matching receiver mode",
    "正在识别协议，等待连续两帧": "Detecting protocol; waiting for two valid frames",
    "一线通监控": "One-wire Monitor",
    "A1.3.3 标准字段；加密、充电策略和互认未实现。公有帧仅保留接入信息，实时测量以 0x3A 为准。": "A1.3.3 fields; encryption, charging policy and mutual recognition are not implemented. Public frames retain connection information; live measurements come from 0x3A.",
    "厂家代码": "Manufacturer code",
    "电芯材料": "Cell chemistry",
    "故障说明": "Fault description",
    "工作状态": "Work status",
    "请求应答设备": "Response requested",
    "充电状态": "Charge status",
    "加密/充电策略/互认": "Encryption / charging policy / mutual recognition",
    "未实现": "Not implemented",
    "未定义/保留": "Undefined / reserved",
    "保留": "Reserved",
    "不需要应答": "No response requested",
    "充电器": "Charger",
    "放电": "Discharging",
    "充电": "Charging",
    "回馈": "Regeneration",
    "在线": "Online",
    "已失联": "Offline",
    "接收器关闭": "Receiver disabled",
    "接入信息": "Connection information",
    "接入信息（离线）": "Connection information (offline)",
    "接收租约已到期，已清空缓存，正在重新启用；等待 BMS 数据。": "Receiver lease expired; cache cleared and receiver restarting. Waiting for BMS data.",
    "测量不可用": "Measurements unavailable",
    "磷酸铁锂": "LFP",
    "锰酸锂": "LMO",
    "三元锂": "NMC",
    "钴酸锂": "LCO",
    "聚合锂": "Lithium polymer",
    "钛酸锂": "LTO",
    "铅酸": "Lead acid",
    "镍氢": "NiMH",
    "钠": "Sodium",
    "无故障": "No fault",
    "放电过流二级保护": "Discharge overcurrent protection II",
    "放电过流一级保护": "Discharge overcurrent protection I",
    "低温充电保护": "Charge undertemperature protection",
    "充电高温保护": "Charge overtemperature protection",
    "放电高温保护": "Discharge overtemperature protection",
    "欠压保护": "Undervoltage protection",
    "过压保护": "Overvoltage protection",
    "充电过流保护": "Charge overcurrent protection",
    "放电低温保护": "Discharge undertemperature protection",
    "充电MOS故障": "Charge MOS fault",
    "放电MOS故障": "Discharge MOS fault",
    "充电高温异常报警": "Charge overtemperature alarm",
    "放电高温异常报警": "Discharge overtemperature alarm",
    "放电过流一级预警": "Discharge overcurrent warning I",
    "低温充电预警": "Charge undertemperature warning",
    "充电高温预警": "Charge overtemperature warning",
    "放电高温预警": "Discharge overtemperature warning",
    "欠压预警": "Undervoltage warning",
    "过压预警": "Overvoltage warning",
    "充电过流预警": "Charge overcurrent warning",
    "放电低温预警": "Discharge undertemperature warning",
    "满充停止": "Charge complete; stop charging",
    "非法充电": "Unauthorized charging",
    "电池保护": "Battery protection",
    "正在充电": "Charging in progress",
    "电池型号": "Battery model",
    "材料代码": "Chemistry code",
    "允许回馈电流": "Allowed feedback current",
    "请求充电电压": "Requested charge voltage",
    "请求充电电流": "Requested charge current",
    "校验失败和过期数据不显示为实时测量值。监控期间串口被独占；停止后恢复其它操作。": "Invalid and stale data are not shown as live measurements. The serial port is reserved until monitoring stops.",
    "请先停止其它监控并等待当前任务完成": "Stop other monitors and wait for the current task to finish.",
    "串口忙": "Serial port busy",
    "通信异常，测量值已清空；上拉将在失联超时后释放：": "Link error; measurements cleared. The pull-up will be released after the link timeout: ",
    "通信异常": "Link error",
    "监控已停止": "Monitoring stopped",
    "最近完整帧": "Last complete frame",
    "等待或过期": "Waiting or stale",
    "有效": "Valid",
    "校验失败": "Checksum errors",
    "接收启用": "Receiver enabled",
    "有效帧": "Good frames",
    "时序错误": "Timing errors",
    "队列溢出": "Queue overflows",
    "协议版本": "Protocol version",
    "故障码": "Fault code",
    "工作状态原值": "Raw work status",
    "BMS状态原值": "Raw BMS status",
    "最高单体位置原值": "Raw max cell index",
    "最低单体位置原值": "Raw min cell index",
    "充电状态原值": "Raw charge status",
    "电池编码": "Battery code",
    "BMS_V1.13.1 - CAN用户上位机": "BMS V1.13.1 - CAN Service Tool",
    "BMS 实时监控": "BMS Live Monitor",
    "BMS 日志": "BMS Event Log",
    "实时监控": "Live Monitor",
    "实时数据": "Live Data",
    "存储信息": "Event Storage",
    "参数设置": "Parameters",
    "系统状态": "System Status",
    "其它功能": "Maintenance",
    "保护参数": "Protection",
    "均衡/限流": "Balance / Current Limit",
    "休眠": "Sleep",
    "SOC/系统": "SOC / System",
    "参数配置": "Parameter Configuration",
    "读取当前页": "Read Current Page",
    "全部读取": "Read All",
    "写入修改": "Write Changes",
    "放弃修改": "Discard Changes",
    "未修改": "No changes",
    "未读取": "Not read",
    "未选择": "Not selected",
    "未连接": "Disconnected",
    "等待操作": "Ready",
    "通信: 未连接": "Link: Disconnected",
    "升级进度: --": "Upgrade: --",
    "老化状态: 未读取": "Aging: Not read",
    "记录: 已停止": "Logging: Stopped",
    "开始监控": "Start Monitor",
    "停止监控": "Stop Monitor",
    "开始记录": "Start Logging",
    "停止记录": "Stop Logging",
    "维护解锁": "Unlock Maintenance",
    "锁定维护功能": "Lock Maintenance",
    "维护密码": "Maintenance Password",
    "请输入维护密码": "Enter maintenance password",
    "密码错误": "Incorrect Password",
    "维护功能已解锁": "Maintenance functions unlocked",
    "维护功能已锁定": "Maintenance functions locked",
    "维护密码不正确。": "Incorrect maintenance password.",
    "客户版": "Customer Edition",
    "内部版": "Internal Edition",
    "单体电压(mV)": "Cell Voltage (mV)",
    "基础信息": "Battery Summary",
    "温度(℃)": "Temperature (°C)",
    "通信设置": "Connection",
    "BMS 版本/序列号": "BMS Version / Serial Number",
    "读取一次": "Read Once",
    "读取BMS日志": "Read BMS Log",
    "重置100条日志": "Reset 100 Logs",
    "显示板端存储的事件记录": "Events stored in the BMS",
    "项目": "Item",
    "数值": "Value",
    "单位": "Unit",
    "序号": "Index",
    "事件": "Event",
    "距离上次": "Since Previous",
    "与上次间隔": "Since Previous",
    "原始值": "Raw Value",
    "参数项": "Parameter",
    "保护值": "Trip",
    "恢复值": "Recovery",
    "延时": "Delay",
    "一级": "Level 1",
    "二级": "Level 2",
    "三级": "Level 3",
    "恢复": "Recovery",
    "电压": "Voltage",
    "时间": "Time",
    "充电电流": "Charge Current",
    "放电电流": "Discharge Current",
    "唤醒时间": "Wake Time",
    "休眠时间": "Sleep Time",
    "正常休眠": "Normal Sleep",
    "过放休眠": "Over-discharge Sleep",
    "电流过滤": "Current Filter",
    "RTC调度": "RTC Schedule",
    "SH309 电压保护": "SH309 Voltage Protection",
    "SH309 充放电过流": "SH309 Charge / Discharge Overcurrent",
    "SH309 充放电温度": "SH309 Charge / Discharge Temperature",
    "MOS 温度保护": "MOS Temperature Protection",
    "AFE 短路保护": "AFE Short-circuit Protection",
    "单体过压": "Cell Overvoltage",
    "单体欠压": "Cell Undervoltage",
    "充电过流一级": "Charge Overcurrent L1",
    "充电过流二级": "Charge Overcurrent L2",
    "放电过流一级": "Discharge Overcurrent L1",
    "放电过流二级": "Discharge Overcurrent L2",
    "充电高温": "Charge High Temp",
    "充电低温": "Charge Low Temp",
    "放电高温": "Discharge High Temp",
    "放电低温": "Discharge Low Temp",
    "MOS过温": "MOS Over-temperature",
    "AFE短路": "AFE Short Circuit",
    "短路电流": "Short-circuit Current",
    "短路延时": "Short-circuit Delay",
    "均衡参数": "Balance Parameters",
    "短路参数": "Short-circuit Parameters",
    "限流参数": "Current-limit Parameters",
    "休眠参数": "Sleep Parameters",
    "SOC参数": "SOC Parameters",
    "系统参数": "System Parameters",
    "休眠条件": "Sleep Conditions",
    "休眠电流过滤": "Sleep Current Filter",
    "RTC 时间": "RTC Timing",
    "MOS过温重置": "Reset MOS Temperature",
    "保护点重置": "Reset Protection",
    "通信方式": "Connection Mode",
    "comm tool/CAN桥": "Comm Tool / CAN Bridge",
    "BMS直连串口": "BMS Direct Serial",
    "Modbus地址": "Modbus Address",
    "串口": "Serial Port",
    "刷新": "Refresh",
    "BMS/串口2波特率": "BMS / UART2 Baud",
    "间隔(s)": "Interval (s)",
    "记录间隔(s)": "Log Interval (s)",
    "选择文件": "Choose File",
    "运行日志": "Operation Log",
    "CAN诊断": "CAN Diagnostics",
    "读取BMS信息": "Read BMS Info",
    "升级文件": "Firmware Image",
    "目标设备": "Target Device",
    "常用功能": "Common Functions",
    "高级寄存器": "Advanced Registers",
    "选择": "Browse",
    "校验文件": "Validate File",
    "CAN波特率": "CAN Bitrate",
    "BMS地址": "BMS Address",
    "IAP节点": "IAP Node",
    "应用设置": "Apply Settings",
    "写SOC": "Write SOC",
    "开启老化模式": "Start Aging",
    "关闭老化模式": "Stop Aging",
    "重置老化时间": "Reset Aging Time",
    "读取老化时间": "Read Aging Time",
    "老化时长(h)": "Aging Duration (h)",
    "修改老化时间": "Set Aging Duration",
    "读取缓存": "Read Cache",
    "写入缓存": "Write Cache",
    "一键升级": "Upgrade",
    "使用缓存升级": "Upgrade from Cache",
    "读取BMS状态": "Read BMS Status",
    "进入IAP": "Enter IAP",
    "地址": "Address",
    "数量": "Count",
    "读取": "Read",
    "写入值": "Values",
    "写入": "Write",
    "comm tool": "Comm Tool",
    "comm tool 缓存": "Comm Tool Cache",
    "当前文件": "Selected Image",
    "总压": "Pack Voltage",
    "电流": "Current",
    "剩余容量": "Remaining Capacity",
    "满电容量": "Full Capacity",
    "出厂容量": "Factory Capacity",
    "循环次数": "Cycle Count",
    "最高温度": "Maximum Temperature",
    "最低温度": "Minimum Temperature",
    "温度1": "Temperature 1",
    "温度2": "Temperature 2",
    "MOS温度": "MOS Temperature",
    "充电MOS": "Charge MOS",
    "放电MOS": "Discharge MOS",
    "加热": "Heating",
    "冷凝": "Cooling",
    "一级告警": "Level 1 Warning",
    "二级告警": "Level 2 Warning",
    "三级保护": "Level 3 Protection",
    "系统监控信息": "System Monitor",
    "多设备时必须保证目标 BMS 地址唯一；多个 BMS 同地址不要同时升级。": (
        "Each target BMS must use a unique address. Do not upgrade multiple BMS units with the same address at once."
    ),
    "预充时间": "Precharge Time",
    "采样电阻数": "Sense Resistor Count",
    "采样电阻": "Sense Resistance",
    "个": "pcs",
    "充电短路范围": "Charge Short-circuit Range",
    "放电短路范围": "Discharge Short-circuit Range",
    "SOC_0电压": "SOC 0% Voltage",
    "SOC_100电压": "SOC 100% Voltage",
    "SOC曲线选择": "SOC Curve Selection",
    "开启压差": "Start Delta",
    "开启电压": "Start Voltage",
    "关闭压差": "Stop Delta",
    "永久密码": "Permanent Password",
    "电池串数": "Cell Count",
    "限流压差": "Current-limit Delta",
    "限流电流": "Current-limit Current",
    "串": "cells",
}


_REPLACE_EN = (
    ("软件版本", "Software Version"),
    ("硬件版本", "Hardware Version"),
    ("BMS序列号", "BMS Serial Number"),
    ("读取失败", "Read failed"),
    ("写入失败", "Write failed"),
    ("操作失败", "Operation failed"),
    ("参数错误", "Parameter error"),
    ("文件不存在", "File not found"),
    ("确认写入参数", "Confirm Parameter Write"),
    ("确认写入", "Confirm Write"),
    ("确认升级", "Confirm Upgrade"),
    ("确认进入IAP", "Confirm Enter IAP"),
    ("确认重置", "Confirm Reset"),
    ("读取完成", "Read complete"),
    ("写入完成", "Write complete"),
    ("升级完成", "Upgrade complete"),
    ("升级失败", "Upgrade failed"),
    ("通信异常", "Communication error"),
    ("通信", "Link"),
    ("正在执行", "Running"),
    ("已停止", "Stopped"),
    ("已修改", "Changed"),
    ("未写入", "not written"),
    ("已读取", "Read"),
    ("已完成", "Complete"),
    ("未响应", "No response"),
    ("未知事件", "Unknown event "),
    ("最高电压", "Maximum Voltage"),
    ("最低电压", "Minimum Voltage"),
    ("平均电压", "Average Voltage"),
    ("最大压差", "Maximum Delta"),
    ("单体", "Cell "),
    ("温度", "Temperature"),
    ("故障", "Fault"),
    ("均衡", "Balance"),
    ("容量", "Capacity"),
    ("充电", "Charge"),
    ("放电", "Discharge"),
    ("过压", "Overvoltage"),
    ("欠压", "Undervoltage"),
    ("过流", "Overcurrent"),
    ("高温", "High Temp"),
    ("低温", "Low Temp"),
    ("短路", "Short Circuit"),
    ("恢复", "Recovery"),
    ("延时", "Delay"),
    ("保护", "Protection"),
    ("告警", "Warning"),
    ("休眠", "Sleep"),
    ("唤醒", "Wake"),
    ("开启", "Start"),
    ("关闭", "Stop"),
    ("重置", "Reset"),
    ("读取", "Read"),
    ("写入", "Write"),
    ("选择", "Select"),
    ("当前", "Current"),
    ("状态", "Status"),
    ("进度", "Progress"),
    ("记录", "Log"),
    ("日志", "Log"),
    ("文件", "File"),
    ("参数", "Parameters"),
    ("串口", "Serial"),
    ("波特率", "Baud"),
    ("节点", "Node"),
    ("地址", "Address"),
    ("确认", "Confirm"),
    ("取消", "Cancel"),
    ("成功", "Success"),
    ("失败", "Failed"),
    ("错误", "Error"),
    ("等待", "Waiting"),
    ("秒", "s"),
    ("分钟", "min"),
    ("小时", "h"),
    ("次", ""),
)


def set_language(language: str) -> None:
    global _language
    _language = LANG_EN if language == LANG_EN else LANG_ZH


def tr(value: Any) -> Any:
    if _language != LANG_EN or not isinstance(value, str) or not value:
        return value
    exact = _EXACT_EN.get(value)
    if exact is not None:
        return exact
    text = value
    for source, target in _REPLACE_EN:
        text = text.replace(source, target)
    return text


def contains_chinese(value: str) -> bool:
    return bool(re.search(r"[\u3400-\u9fff]", value))


def _patch_constructor(cls: type) -> None:
    original = cls.__init__

    def localized_init(self, *args, **kwargs):
        if "text" in kwargs:
            kwargs["text"] = tr(kwargs["text"])
        if "values" in kwargs:
            kwargs["values"] = tuple(tr(item) for item in kwargs["values"])
        original(self, *args, **kwargs)

    cls.__init__ = localized_init


def _patch_dialog(function: Callable) -> Callable:
    def localized(*args, **kwargs):
        args = tuple(tr(item) if isinstance(item, str) else item for item in args)
        if "title" in kwargs:
            kwargs["title"] = tr(kwargs["title"])
        if "message" in kwargs:
            kwargs["message"] = tr(kwargs["message"])
        return function(*args, **kwargs)

    return localized


def install_ui_language(language: str) -> None:
    global _patched
    set_language(language)
    if language != LANG_EN or _patched:
        return
    _patched = True

    original_string_var = tk.StringVar

    class LocalizedStringVar(original_string_var):
        def __init__(self, *args, **kwargs):
            if "value" in kwargs:
                kwargs["value"] = tr(kwargs["value"])
            super().__init__(*args, **kwargs)

        def set(self, value):
            super().set(tr(value))

    tk.StringVar = LocalizedStringVar

    for cls in (tk.Label, tk.Button, ttk.Label, ttk.Button, ttk.LabelFrame, ttk.Checkbutton, ttk.Radiobutton, ttk.Combobox):
        _patch_constructor(cls)

    original_notebook_add = ttk.Notebook.add

    def notebook_add(self, child, **kwargs):
        if "text" in kwargs:
            kwargs["text"] = tr(kwargs["text"])
        return original_notebook_add(self, child, **kwargs)

    ttk.Notebook.add = notebook_add

    original_heading = ttk.Treeview.heading

    def tree_heading(self, column, option=None, **kwargs):
        if "text" in kwargs:
            kwargs["text"] = tr(kwargs["text"])
        return original_heading(self, column, option, **kwargs)

    ttk.Treeview.heading = tree_heading

    original_insert = ttk.Treeview.insert

    def tree_insert(self, parent, index, iid=None, **kwargs):
        if "text" in kwargs:
            kwargs["text"] = tr(kwargs["text"])
        if "values" in kwargs:
            kwargs["values"] = tuple(tr(item) for item in kwargs["values"])
        if iid is None:
            return original_insert(self, parent, index, **kwargs)
        return original_insert(self, parent, index, iid=iid, **kwargs)

    ttk.Treeview.insert = tree_insert

    for name in ("showinfo", "showerror", "showwarning", "askyesno", "askokcancel"):
        setattr(messagebox, name, _patch_dialog(getattr(messagebox, name)))
    for name in ("askopenfilename", "asksaveasfilename"):
        setattr(filedialog, name, _patch_dialog(getattr(filedialog, name)))

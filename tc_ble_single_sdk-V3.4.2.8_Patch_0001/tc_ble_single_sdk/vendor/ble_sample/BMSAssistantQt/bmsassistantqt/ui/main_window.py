from __future__ import annotations

import csv
import json
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
from typing import Iterable

from PySide6.QtCore import QSettings, Qt, QTimer
from PySide6.QtGui import QCloseEvent
from PySide6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMainWindow,
    QPlainTextEdit,
    QPushButton,
    QScrollArea,
    QSplitter,
    QTabWidget,
    QTableWidget,
    QTableWidgetItem,
    QTextEdit,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from ..app_controller import AppController
from ..models import BatteryStatusSnapshot, ConnectionStatus, RegisterBlock, ScanMode, battery_status_flags


APP_STYLESHEET = """
QMainWindow, QWidget {
    background: #f6fbf8;
    color: #1f2937;
    font-size: 13px;
}
QGroupBox {
    background: #ffffff;
    border: 1px solid #d8e7df;
    border-radius: 14px;
    margin-top: 12px;
    font-weight: 600;
    padding-top: 12px;
}
QGroupBox::title {
    left: 14px;
    top: 2px;
    subcontrol-origin: margin;
    color: #0f5132;
}
QLineEdit, QPlainTextEdit, QTextEdit, QTreeWidget, QTableWidget, QComboBox {
    background: #fbfdfc;
    border: 1px solid #d7e5df;
    border-radius: 10px;
    padding: 6px 8px;
    selection-background-color: #cfece3;
}
QPushButton {
    background: #eaf7f2;
    border: 1px solid #c7e6da;
    border-radius: 10px;
    padding: 7px 14px;
}
QPushButton:hover {
    background: #ddf1e9;
}
QPushButton:disabled {
    color: #8ca39b;
    background: #f2f6f4;
    border-color: #e0ebe6;
}
QTreeWidget, QTableWidget {
    alternate-background-color: #f5faf7;
    gridline-color: #e4efea;
}
QHeaderView::section {
    background: #edf7f2;
    color: #215b45;
    border: none;
    border-bottom: 1px solid #d8e7df;
    padding: 7px 8px;
}
QScrollArea {
    border: none;
}
QTabWidget::pane {
    border: none;
}
QTabBar::tab {
    background: #edf7f2;
    border: 1px solid #cfe3d9;
    border-bottom: none;
    padding: 8px 18px;
    border-top-left-radius: 10px;
    border-top-right-radius: 10px;
    margin-right: 6px;
}
QTabBar::tab:selected {
    background: #ffffff;
    color: #0b6b53;
}
QLabel[role="muted"] {
    color: #6b7280;
}
QLabel[role="title"] {
    font-size: 28px;
    font-weight: 700;
    color: #0f172a;
}
QLabel[role="sectionValue"] {
    color: #0f172a;
    font-weight: 600;
}
QFrame#metricCard {
    background: #ffffff;
    border: 1px solid #d8e7df;
    border-radius: 14px;
}
QLabel[badgeTone="neutral"] {
    background: #edf2f7;
    color: #334155;
    border-radius: 12px;
    padding: 5px 10px;
}
QLabel[badgeTone="success"] {
    background: #dcfce7;
    color: #166534;
    border-radius: 12px;
    padding: 5px 10px;
}
QLabel[badgeTone="warning"] {
    background: #fef3c7;
    color: #92400e;
    border-radius: 12px;
    padding: 5px 10px;
}
QLabel[badgeTone="danger"] {
    background: #fee2e2;
    color: #991b1b;
    border-radius: 12px;
    padding: 5px 10px;
}
QLabel[badgeTone="info"] {
    background: #dbeafe;
    color: #1d4ed8;
    border-radius: 12px;
    padding: 5px 10px;
}
"""


class BadgeLabel(QLabel):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setProperty("badgeTone", "neutral")
        self.setMinimumHeight(28)

    def set_badge(self, text: str, tone: str = "neutral") -> None:
        self.setText(text)
        self.setProperty("badgeTone", tone)
        self.style().unpolish(self)
        self.style().polish(self)


class MetricCard(QFrame):
    def __init__(self, title: str, compact: bool = False, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("metricCard")
        self._compact = compact

        layout = QVBoxLayout(self)
        layout.setContentsMargins(14, 14, 14, 14)
        layout.setSpacing(8)

        self.title_label = QLabel(title)
        self.title_label.setProperty("role", "muted")
        self.value_label = QLabel("—")
        self.detail_label = QLabel("—")
        self.detail_label.setProperty("role", "muted")
        self.detail_label.setWordWrap(True)

        value_size = 24 if not compact else 18
        self.value_label.setStyleSheet(f"font-size: {value_size}px; font-weight: 700; color: #0b6b53;")

        layout.addWidget(self.title_label)
        layout.addWidget(self.value_label)
        layout.addWidget(self.detail_label)

        self.setMinimumHeight(116 if not compact else 96)

    def set_content(self, value: str, detail: str, color: str = "#0b6b53") -> None:
        self.value_label.setText(value)
        self.detail_label.setText(detail)
        self.value_label.setStyleSheet(
            f"font-size: {'24' if not self._compact else '18'}px; font-weight: 700; color: {color};"
        )


class MainWindow(QMainWindow):
    def __init__(self, controller: AppController | None = None) -> None:
        super().__init__()
        self.controller = controller or AppController(self)
        self._settings = QSettings("cs", "BMSAssistantQt")
        self._battery_refresh_timer = QTimer(self)
        self._battery_refresh_timer.timeout.connect(self._on_battery_refresh_timeout)

        self.metric_cards: dict[str, MetricCard] = {}
        self.cell_cards: list[MetricCard] = []
        self.flag_badges: dict[str, BadgeLabel] = {}

        self._build_ui()
        self._restore_settings()
        self._connect_signals()
        self._reload_all()

    def _build_ui(self) -> None:
        self.setWindowTitle("BMSAssistantQt")
        self.resize(1560, 980)
        self.setStyleSheet(APP_STYLESHEET)

        central = QWidget(self)
        self.setCentralWidget(central)

        root_layout = QVBoxLayout(central)
        root_layout.setContentsMargins(16, 16, 16, 16)

        splitter = QSplitter(Qt.Orientation.Horizontal, central)
        root_layout.addWidget(splitter)

        sidebar = self._build_sidebar()
        detail = self._build_detail_panel()

        splitter.addWidget(sidebar)
        splitter.addWidget(detail)
        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([410, 1120])

    def _build_sidebar(self) -> QWidget:
        container = QWidget(self)
        layout = QVBoxLayout(container)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(14)

        title = QLabel("BMS Assistant Qt")
        title.setProperty("role", "title")
        subtitle = QLabel("Qt 跨平台 BLE 调试上位机")
        subtitle.setProperty("role", "muted")

        title_wrap = QWidget(self)
        title_layout = QVBoxLayout(title_wrap)
        title_layout.setContentsMargins(4, 4, 4, 4)
        title_layout.addWidget(title)
        title_layout.addWidget(subtitle)
        layout.addWidget(title_wrap)

        scan_box = QGroupBox("扫描控制", self)
        scan_layout = QVBoxLayout(scan_box)
        self.scan_mode_combo = QComboBox(scan_box)
        for mode in ScanMode:
            self.scan_mode_combo.addItem(mode.value, mode.value)
        self.search_input = QLineEdit(scan_box)
        self.search_input.setPlaceholderText("设备名过滤，例如 BT")
        self.only_likely_checkbox = QCheckBox("只显示疑似 BMS 设备", scan_box)
        scan_note = QLabel("当前 Qt 版先扫描全部 BLE，再按 `BT* / 180F / 1812` 过滤显示。")
        scan_note.setWordWrap(True)
        scan_note.setProperty("role", "muted")

        scan_form = QFormLayout()
        scan_form.addRow("扫描模式", self.scan_mode_combo)
        scan_form.addRow("设备过滤", self.search_input)
        scan_layout.addLayout(scan_form)
        scan_layout.addWidget(self.only_likely_checkbox)
        scan_layout.addWidget(scan_note)

        button_row = QHBoxLayout()
        self.scan_button = QPushButton("开始扫描", scan_box)
        self.clear_devices_button = QPushButton("清空列表", scan_box)
        self.connect_button = QPushButton("连接所选设备", scan_box)
        self.disconnect_button = QPushButton("断开", scan_box)
        for widget in [
            self.scan_button,
            self.clear_devices_button,
            self.connect_button,
            self.disconnect_button,
        ]:
            button_row.addWidget(widget)
        scan_layout.addLayout(button_row)

        badge_row = QHBoxLayout()
        self.bluetooth_badge = BadgeLabel(scan_box)
        self.connection_badge = BadgeLabel(scan_box)
        badge_row.addWidget(self.bluetooth_badge)
        badge_row.addWidget(self.connection_badge)
        scan_layout.addLayout(badge_row)

        self.device_count_label = QLabel("已发现 0 台设备", scan_box)
        self.device_count_label.setProperty("role", "muted")
        scan_layout.addWidget(self.device_count_label)
        layout.addWidget(scan_box)

        device_box = QGroupBox("设备列表", self)
        device_layout = QVBoxLayout(device_box)
        self.device_tree = QTreeWidget(device_box)
        self.device_tree.setColumnCount(4)
        self.device_tree.setHeaderLabels(["设备", "RSSI", "广播 UUID", "状态"])
        self.device_tree.setRootIsDecorated(False)
        self.device_tree.setAlternatingRowColors(True)
        self.device_tree.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.device_tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        self.device_tree.header().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.device_tree.header().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
        self.device_tree.header().setSectionResizeMode(3, QHeaderView.ResizeMode.ResizeToContents)
        device_layout.addWidget(self.device_tree)
        layout.addWidget(device_box, 1)

        status_box = QGroupBox("当前状态", self)
        status_layout = QVBoxLayout(status_box)
        self.status_label = QLabel("等待蓝牙初始化", status_box)
        self.status_label.setWordWrap(True)
        status_layout.addWidget(self.status_label)
        layout.addWidget(status_box)

        return container

    def _build_detail_panel(self) -> QWidget:
        container = QWidget(self)
        layout = QVBoxLayout(container)
        layout.setContentsMargins(0, 0, 0, 0)

        self.tab_widget = QTabWidget(container)
        self.tab_widget.addTab(self._build_battery_tab(), "电池状态")
        self.tab_widget.addTab(self._build_debug_tab(), "调试工作台")
        layout.addWidget(self.tab_widget)
        return container

    def _build_battery_tab(self) -> QWidget:
        scroll = QScrollArea(self)
        scroll.setWidgetResizable(True)
        wrapper = QWidget(scroll)
        layout = QVBoxLayout(wrapper)
        layout.setSpacing(14)

        overview_box = QGroupBox("电池状态总览", wrapper)
        overview_layout = QVBoxLayout(overview_box)
        top_row = QHBoxLayout()
        left_col = QVBoxLayout()
        self.battery_device_label = QLabel("未连接")
        self.battery_device_label.setProperty("role", "title")
        self.battery_overview_note = QLabel(
            "这一页只展示业务数据，不放寄存器调试控件。优先读取 `0xD120~0xD12A`，旧固件自动回退到 `0xD000~0xD03E` 与 `0xD115~0xD116`。"
        )
        self.battery_overview_note.setWordWrap(True)
        self.battery_overview_note.setProperty("role", "muted")
        left_col.addWidget(self.battery_device_label)
        left_col.addWidget(self.battery_overview_note)
        top_row.addLayout(left_col, 1)

        right_col = QVBoxLayout()
        action_row = QHBoxLayout()
        self.battery_refresh_button = QPushButton("刷新状态", overview_box)
        self.auto_refresh_checkbox = QCheckBox("自动刷新", overview_box)
        self.auto_refresh_checkbox.setChecked(True)
        self.auto_refresh_checkbox.setToolTip("仅在“电池状态”页且链路可收发时生效")
        self.refresh_interval_combo = QComboBox(overview_box)
        for title, interval in [("1s", 1000), ("2s", 2000), ("5s", 5000)]:
            self.refresh_interval_combo.addItem(title, interval)
        self.refresh_interval_combo.setCurrentIndex(1)
        action_row.addWidget(self.battery_refresh_button)
        action_row.addWidget(self.auto_refresh_checkbox)
        action_row.addWidget(self.refresh_interval_combo)
        right_col.addLayout(action_row)

        badge_row = QHBoxLayout()
        self.direction_badge = BadgeLabel(overview_box)
        self.source_badge = BadgeLabel(overview_box)
        badge_row.addWidget(self.direction_badge)
        badge_row.addWidget(self.source_badge)
        right_col.addLayout(badge_row)

        self.battery_updated_label = QLabel("更新时间：未刷新", overview_box)
        self.battery_updated_label.setProperty("role", "muted")
        right_col.addWidget(self.battery_updated_label)
        right_col.addStretch(1)
        top_row.addLayout(right_col)
        overview_layout.addLayout(top_row)

        self.battery_source_note = QLabel("尚未建立数据快照。", overview_box)
        self.battery_source_note.setProperty("role", "muted")
        self.battery_source_note.setWordWrap(True)
        overview_layout.addWidget(self.battery_source_note)
        layout.addWidget(overview_box)

        layout.addWidget(self._build_metric_box("关键指标", [
            ("pack_voltage", "Pack Voltage"),
            ("pack_current", "Pack Current"),
            ("soc", "SOC"),
            ("max_temp", "Max Temp"),
            ("min_temp", "Min Temp"),
            ("mos_temp", "MOS Temp"),
        ]))
        layout.addWidget(self._build_metric_box("辅助指标", [
            ("cell_max", "Cell Max"),
            ("cell_min", "Cell Min"),
            ("cell_delta", "Cell Delta"),
        ]))
        layout.addWidget(self._build_metric_box("SOC 与容量", [
            ("soh", "SOH"),
            ("cycle_count", "Cycle Count"),
            ("capacity_now", "Capacity Now"),
            ("capacity_full", "Capacity Full"),
            ("capacity_factory", "Capacity Factory"),
        ]))

        cell_box = QGroupBox("单串电压", wrapper)
        cell_layout = QGridLayout(cell_box)
        cell_layout.setSpacing(10)
        for index in range(10):
            card = MetricCard(f"Cell {index + 1}", compact=True, parent=cell_box)
            self.cell_cards.append(card)
            cell_layout.addWidget(card, index // 5, index % 5)
        layout.addWidget(cell_box)

        flags_box = QGroupBox("系统状态字", wrapper)
        flags_layout = QVBoxLayout(flags_box)
        status_info_row = QHBoxLayout()
        self.system_status_label = QLabel("SystemStatus: 0x00000000", flags_box)
        self.system_status_label.setProperty("role", "sectionValue")
        self.active_flag_count_label = QLabel("活动标志数: 0", flags_box)
        self.active_flag_count_label.setProperty("role", "muted")
        status_info_row.addWidget(self.system_status_label)
        status_info_row.addStretch(1)
        status_info_row.addWidget(self.active_flag_count_label)
        flags_layout.addLayout(status_info_row)

        badge_grid = QGridLayout()
        for index, flag in enumerate(battery_status_flags(0)):
            badge = BadgeLabel(flags_box)
            badge.set_badge(flag.title, "neutral")
            self.flag_badges[flag.key] = badge
            badge_grid.addWidget(badge, index // 4, index % 4)
        flags_layout.addLayout(badge_grid)
        layout.addWidget(flags_box)

        raw_box = QGroupBox("兼容原始测量", wrapper)
        raw_form = QFormLayout(raw_box)
        self.pack_voltage_mirror_label = QLabel("—", raw_box)
        self.battery_temp_adc_label = QLabel("—", raw_box)
        self.mos_temp_adc_label = QLabel("—", raw_box)
        raw_form.addRow("Pack Voltage Mirror", self.pack_voltage_mirror_label)
        raw_form.addRow("Battery Temp ADC", self.battery_temp_adc_label)
        raw_form.addRow("MOS Temp ADC", self.mos_temp_adc_label)
        layout.addWidget(raw_box)

        version_box = QGroupBox("连接与版本", wrapper)
        version_form = QFormLayout(version_box)
        self.connection_info_labels = {
            "device": QLabel("未连接", version_box),
            "status": QLabel(ConnectionStatus.IDLE.value, version_box),
            "display_name": QLabel("—", version_box),
            "software": QLabel("—", version_box),
            "serial": QLabel("—", version_box),
            "mac": QLabel("—", version_box),
        }
        version_form.addRow("连接设备", self.connection_info_labels["device"])
        version_form.addRow("连接状态", self.connection_info_labels["status"])
        version_form.addRow("显示名称", self.connection_info_labels["display_name"])
        version_form.addRow("软件版本", self.connection_info_labels["software"])
        version_form.addRow("序列号", self.connection_info_labels["serial"])
        version_form.addRow("MAC", self.connection_info_labels["mac"])
        layout.addWidget(version_box)

        snapshot_box = QGroupBox("寄存器快照", wrapper)
        snapshot_layout = QVBoxLayout(snapshot_box)
        snapshot_top_row = QHBoxLayout()
        snapshot_top_row.addStretch(1)
        self.export_battery_snapshot_button = QPushButton("导出快照", snapshot_box)
        snapshot_top_row.addWidget(self.export_battery_snapshot_button)
        snapshot_layout.addLayout(snapshot_top_row)
        self.battery_snapshot_edit = QPlainTextEdit(snapshot_box)
        self.battery_snapshot_edit.setReadOnly(True)
        self.battery_snapshot_edit.setMinimumHeight(220)
        snapshot_layout.addWidget(self.battery_snapshot_edit)
        layout.addWidget(snapshot_box)

        layout.addStretch(1)
        scroll.setWidget(wrapper)
        return scroll

    def _build_metric_box(self, title: str, items: list[tuple[str, str]]) -> QGroupBox:
        box = QGroupBox(title, self)
        layout = QGridLayout(box)
        layout.setSpacing(10)
        for index, (key, label) in enumerate(items):
            card = MetricCard(label, parent=box)
            self.metric_cards[key] = card
            layout.addWidget(card, index // 3, index % 3)
        return box

    def _build_debug_tab(self) -> QWidget:
        scroll = QScrollArea(self)
        scroll.setWidgetResizable(True)
        wrapper = QWidget(scroll)
        layout = QVBoxLayout(wrapper)
        layout.setSpacing(14)

        session_box = QGroupBox("会话概览", wrapper)
        session_layout = QHBoxLayout(session_box)
        left_col = QVBoxLayout()
        self.debug_device_label = QLabel("未连接", session_box)
        self.debug_device_label.setProperty("role", "title")
        debug_desc = QLabel("面向 `vendor/ble_sample` 的 BLE 调试上位机。业务链路为 `Modbus RTU over Telink SPP`。", session_box)
        debug_desc.setWordWrap(True)
        debug_desc.setProperty("role", "muted")
        left_col.addWidget(self.debug_device_label)
        left_col.addWidget(debug_desc)
        session_layout.addLayout(left_col, 1)

        right_col = QVBoxLayout()
        self.debug_bt_badge = BadgeLabel(session_box)
        self.debug_link_badge = BadgeLabel(session_box)
        self.debug_busy_label = QLabel("空闲", session_box)
        self.debug_busy_label.setProperty("role", "muted")
        right_col.addWidget(self.debug_bt_badge)
        right_col.addWidget(self.debug_link_badge)
        right_col.addWidget(self.debug_busy_label)
        session_layout.addLayout(right_col)
        layout.addWidget(session_box)

        action_box = QGroupBox("快捷调试动作", wrapper)
        action_layout = QGridLayout(action_box)
        self.identity_button = QPushButton("刷新身份", action_box)
        self.system_status_button = QPushButton("系统状态", action_box)
        self.protect_button = QPushButton("保护参数", action_box)
        self.event_log_button = QPushButton("事件日志", action_box)
        self.quick_manual_read_button = QPushButton("手动读寄存器", action_box)
        self.echo_button = QPushButton("Echo 测试", action_box)
        for index, widget in enumerate([
            self.identity_button,
            self.system_status_button,
            self.protect_button,
            self.event_log_button,
            self.quick_manual_read_button,
            self.echo_button,
        ]):
            action_layout.addWidget(widget, index // 3, index % 3)
        layout.addWidget(action_box)

        identity_box = QGroupBox("设备身份", wrapper)
        identity_form = QFormLayout(identity_box)
        self.identity_labels = {
            "connected_device": QLabel("未连接", identity_box),
            "display_name": QLabel("—", identity_box),
            "mac": QLabel("—", identity_box),
            "serial": QLabel("—", identity_box),
            "hardware": QLabel("—", identity_box),
            "software": QLabel("—", identity_box),
        }
        identity_form.addRow("连接设备", self.identity_labels["connected_device"])
        identity_form.addRow("显示名称", self.identity_labels["display_name"])
        identity_form.addRow("MAC", self.identity_labels["mac"])
        identity_form.addRow("序列号", self.identity_labels["serial"])
        identity_form.addRow("硬件版本", self.identity_labels["hardware"])
        identity_form.addRow("软件版本", self.identity_labels["software"])
        layout.addWidget(identity_box)

        workbench_row = QHBoxLayout()
        register_box = QGroupBox("寄存器读写工作台", wrapper)
        register_layout = QVBoxLayout(register_box)
        register_layout.addWidget(QLabel("读取保持寄存器", register_box))
        read_row = QHBoxLayout()
        self.manual_read_address_edit = QLineEdit(register_box)
        self.manual_read_address_edit.setPlaceholderText("起始地址，例如 0x0000")
        self.manual_read_quantity_edit = QLineEdit(register_box)
        self.manual_read_quantity_edit.setPlaceholderText("数量，例如 3")
        read_row.addWidget(self.manual_read_address_edit)
        read_row.addWidget(self.manual_read_quantity_edit)
        register_layout.addLayout(read_row)
        self.manual_read_button = QPushButton("执行读取", register_box)
        register_layout.addWidget(self.manual_read_button)

        register_layout.addWidget(QLabel("写寄存器", register_box))
        self.manual_write_address_edit = QLineEdit(register_box)
        self.manual_write_address_edit.setPlaceholderText("写入地址，例如 0x1005")
        self.manual_write_words_edit = QLineEdit(register_box)
        self.manual_write_words_edit.setPlaceholderText("写入值，例如 0x0001, 0x0002")
        self.manual_write_button = QPushButton("执行写入", register_box)
        register_layout.addWidget(self.manual_write_address_edit)
        register_layout.addWidget(self.manual_write_words_edit)
        register_layout.addWidget(self.manual_write_button)

        quick_row = QHBoxLayout()
        self.quick_soc_edit = QLineEdit(register_box)
        self.quick_soc_edit.setPlaceholderText("SOC，例如 60")
        self.quick_soc_button = QPushButton("写 SOC -> 0x1005", register_box)
        self.quick_1103_button = QPushButton("写 0x1103 = 0x0003", register_box)
        quick_row.addWidget(self.quick_soc_edit)
        quick_row.addWidget(self.quick_soc_button)
        quick_row.addWidget(self.quick_1103_button)
        register_layout.addLayout(quick_row)

        write_note = QLabel("当前固件 BLE 单包安全长度为 20 byte，`0x10` 写多寄存器建议不超过 5 words。", register_box)
        write_note.setWordWrap(True)
        write_note.setProperty("role", "muted")
        register_layout.addWidget(write_note)
        workbench_row.addWidget(register_box, 1)

        raw_box = QGroupBox("原始帧与蓝牙名", wrapper)
        raw_layout = QVBoxLayout(raw_box)
        raw_layout.addWidget(QLabel("原始 Modbus RTU 帧", raw_box))
        self.raw_frame_edit = QTextEdit(raw_box)
        self.raw_frame_edit.setMinimumHeight(110)
        self.raw_send_button = QPushButton("发送原始帧", raw_box)
        raw_layout.addWidget(self.raw_frame_edit)
        raw_layout.addWidget(self.raw_send_button)
        raw_example = QLabel("示例：`01 03 00 00 00 03 05 CB`", raw_box)
        raw_example.setProperty("role", "muted")
        raw_layout.addWidget(raw_example)

        raw_layout.addWidget(QLabel("蓝牙名后缀", raw_box))
        self.bt_name_edit = QLineEdit(raw_box)
        self.bt_name_edit.setPlaceholderText("例如 FD1901A")
        self.bt_name_button = QPushButton("写入蓝牙名", raw_box)
        raw_layout.addWidget(self.bt_name_edit)
        raw_layout.addWidget(self.bt_name_button)
        bt_name_note = QLabel("当前固件通过 BLE 写蓝牙名时，suffix 建议不超过 10 个 ASCII 字节。", raw_box)
        bt_name_note.setWordWrap(True)
        bt_name_note.setProperty("role", "muted")
        raw_layout.addWidget(bt_name_note)
        workbench_row.addWidget(raw_box, 1)
        layout.addLayout(workbench_row)

        response_box = QGroupBox("最近响应", wrapper)
        response_layout = QVBoxLayout(response_box)
        self.response_preview_edit = QPlainTextEdit(response_box)
        self.response_preview_edit.setReadOnly(True)
        self.response_preview_edit.setMinimumHeight(120)
        response_layout.addWidget(self.response_preview_edit)
        layout.addWidget(response_box)

        blocks_box = QGroupBox("最近寄存器块", wrapper)
        blocks_layout = QVBoxLayout(blocks_box)
        self.debug_blocks_edit = QPlainTextEdit(blocks_box)
        self.debug_blocks_edit.setReadOnly(True)
        self.debug_blocks_edit.setMinimumHeight(220)
        blocks_layout.addWidget(self.debug_blocks_edit)
        layout.addWidget(blocks_box)

        logs_box = QGroupBox("报文日志", wrapper)
        logs_layout = QVBoxLayout(logs_box)
        logs_top_row = QHBoxLayout()
        self.log_count_label = QLabel("最近 0 条", logs_box)
        self.log_count_label.setProperty("role", "muted")
        self.export_logs_button = QPushButton("导出日志", logs_box)
        self.clear_logs_button = QPushButton("清空日志", logs_box)
        logs_top_row.addWidget(self.log_count_label)
        logs_top_row.addStretch(1)
        logs_top_row.addWidget(self.export_logs_button)
        logs_top_row.addWidget(self.clear_logs_button)
        logs_layout.addLayout(logs_top_row)

        self.logs_table = QTableWidget(logs_box)
        self.logs_table.setColumnCount(5)
        self.logs_table.setHorizontalHeaderLabels(["时间", "方向", "标题", "Payload", "说明"])
        self.logs_table.setAlternatingRowColors(True)
        self.logs_table.setSelectionMode(QAbstractItemView.SelectionMode.NoSelection)
        self.logs_table.verticalHeader().setVisible(False)
        self.logs_table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        self.logs_table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.logs_table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self.logs_table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        self.logs_table.horizontalHeader().setSectionResizeMode(4, QHeaderView.ResizeMode.Stretch)
        logs_layout.addWidget(self.logs_table)
        layout.addWidget(logs_box)

        layout.addStretch(1)
        scroll.setWidget(wrapper)
        return scroll

    def _connect_signals(self) -> None:
        self.scan_mode_combo.currentTextChanged.connect(self.controller.set_scan_mode)
        self.search_input.textChanged.connect(self.controller.set_search_text)
        self.only_likely_checkbox.toggled.connect(self.controller.set_show_only_likely_bms)

        self.scan_button.clicked.connect(lambda: self._run_action(self.controller.toggle_scan))
        self.clear_devices_button.clicked.connect(lambda: self._run_action(self.controller.clear_devices))
        self.connect_button.clicked.connect(lambda: self._run_action(self.controller.connect_selected))
        self.disconnect_button.clicked.connect(lambda: self._run_action(self.controller.disconnect))

        self.device_tree.itemSelectionChanged.connect(self._on_device_selection_changed)
        self.tab_widget.currentChanged.connect(lambda _: self._update_auto_refresh_state())
        self.auto_refresh_checkbox.toggled.connect(self._on_auto_refresh_toggled)
        self.refresh_interval_combo.currentIndexChanged.connect(lambda _: self._update_auto_refresh_state())

        self.battery_refresh_button.clicked.connect(lambda: self._run_action(self.controller.refresh_battery_status))
        self.export_battery_snapshot_button.clicked.connect(self._export_battery_snapshot)
        self.identity_button.clicked.connect(lambda: self._run_action(self.controller.refresh_identity))
        self.system_status_button.clicked.connect(lambda: self._run_action(self.controller.read_system_status))
        self.protect_button.clicked.connect(lambda: self._run_action(self.controller.read_protect_preview))
        self.event_log_button.clicked.connect(lambda: self._run_action(self.controller.read_event_log_preview))
        self.echo_button.clicked.connect(lambda: self._run_action(self.controller.send_echo_test))
        self.quick_manual_read_button.clicked.connect(self._handle_manual_read)

        self.manual_read_button.clicked.connect(self._handle_manual_read)
        self.manual_write_button.clicked.connect(self._handle_manual_write)
        self.quick_soc_button.clicked.connect(self._handle_quick_soc)
        self.quick_1103_button.clicked.connect(lambda: self._run_action(self.controller.write_debug_1103_shortcut))
        self.raw_send_button.clicked.connect(self._handle_raw_send)
        self.bt_name_button.clicked.connect(self._handle_bt_name_write)
        self.export_logs_button.clicked.connect(self._export_logs)
        self.clear_logs_button.clicked.connect(lambda: self._run_action(self.controller.clear_logs))

        self.controller.devicesChanged.connect(self._reload_device_list)
        self.controller.statusChanged.connect(self._refresh_status_widgets)
        self.controller.identityChanged.connect(self._refresh_identity_views)
        self.controller.batteryChanged.connect(self._refresh_battery_page)
        self.controller.blocksChanged.connect(self._refresh_block_views)
        self.controller.logsChanged.connect(self._refresh_logs)
        self.controller.responsePreviewChanged.connect(self._refresh_response_preview)

    def _reload_all(self) -> None:
        self.scan_mode_combo.setCurrentText(self.controller.scan_mode.value)
        self.search_input.setText(self.controller.search_text)
        self.only_likely_checkbox.setChecked(self.controller.show_only_likely_bms)

        self.manual_read_address_edit.setText(self.controller.manual_read_address)
        self.manual_read_quantity_edit.setText(self.controller.manual_read_quantity)
        self.manual_write_address_edit.setText(self.controller.manual_write_address)
        self.manual_write_words_edit.setText(self.controller.manual_write_words)
        self.quick_soc_edit.setText(self.controller.quick_soc_value)
        self.raw_frame_edit.setPlainText(self.controller.raw_hex_command)
        self.bt_name_edit.setText(self.controller.bt_name_suffix)

        self._reload_device_list()
        self._refresh_status_widgets()
        self._refresh_identity_views()
        self._refresh_battery_page()
        self._refresh_block_views()
        self._refresh_response_preview()
        self._refresh_logs()

    def _reload_device_list(self) -> None:
        selected = self.controller.selected_device_id
        self.device_tree.blockSignals(True)
        self.device_tree.clear()

        for device in self.controller.filtered_devices:
            name_lines = [device.display_name]
            if device.alternate_name:
                name_lines.append(f"别名: {device.alternate_name}")
            name_lines.append(f"ID: {device.id[-8:]}")
            status = "已连接" if device.is_connected else ("疑似 BMS" if device.is_likely_bms else "普通设备")
            item = QTreeWidgetItem(["\n".join(name_lines), device.rssi_summary, device.advertised_services_summary, status])
            item.setData(0, Qt.ItemDataRole.UserRole, device.id)
            item.setToolTip(0, device.id)
            item.setToolTip(2, device.advertised_services_summary)
            self.device_tree.addTopLevelItem(item)
            if device.id == selected:
                self.device_tree.setCurrentItem(item)

        self.device_tree.blockSignals(False)
        self.device_count_label.setText(
            f"已发现 {len(self.controller.devices)} 台设备，当前显示 {len(self.controller.filtered_devices)} 台"
        )
        self._refresh_status_widgets()

    def _refresh_status_widgets(self) -> None:
        self.status_label.setText(self.controller.status_message)
        self.scan_button.setText("停止扫描" if self.controller.is_scanning else "开始扫描")

        self.bluetooth_badge.set_badge(f"Bluetooth: {self.controller.bluetooth_state_label}", self._bluetooth_tone())
        self.connection_badge.set_badge(f"链路: {self.controller.connection_status.value}", self._connection_tone())

        self.debug_bt_badge.set_badge(f"Bluetooth: {self.controller.bluetooth_state_label}", self._bluetooth_tone())
        self.debug_link_badge.set_badge(f"连接: {self.controller.connection_status.value}", self._connection_tone())
        self.debug_busy_label.setText(f"执行中：{self.controller.busy_command_name}" if self.controller.busy_command_name else "空闲")

        self.battery_device_label.setText(self.controller.connected_device_name)
        self.debug_device_label.setText(self.controller.connected_device_name)
        self.connection_info_labels["device"].setText(self.controller.connected_device_name)
        self.connection_info_labels["status"].setText(self.controller.connection_status.value)
        self.identity_labels["connected_device"].setText(self.controller.connected_device_name)

        selected_ok = self.controller.selected_device_id is not None
        can_send = self.controller.can_send_commands

        self.connect_button.setEnabled(selected_ok and self.controller.busy_command_name is None)
        self.disconnect_button.setEnabled(self.controller.connection_status in {
            ConnectionStatus.CONNECTING,
            ConnectionStatus.CONNECTED,
            ConnectionStatus.READY,
        })
        self.battery_refresh_button.setEnabled(can_send)

        for widget in [
            self.identity_button,
            self.system_status_button,
            self.protect_button,
            self.event_log_button,
            self.quick_manual_read_button,
            self.manual_read_button,
            self.echo_button,
            self.manual_write_button,
            self.quick_soc_button,
            self.quick_1103_button,
            self.raw_send_button,
            self.bt_name_button,
        ]:
            widget.setEnabled(can_send)

        self._update_auto_refresh_state()

    def _refresh_identity_views(self) -> None:
        self.identity_labels["display_name"].setText(self.controller.identity.display_name)
        self.identity_labels["mac"].setText(self.controller.identity.mac_address)
        self.identity_labels["serial"].setText(self.controller.identity.serial_number)
        self.identity_labels["hardware"].setText(self.controller.identity.hardware_version)
        self.identity_labels["software"].setText(self.controller.identity.software_version)

        self.connection_info_labels["display_name"].setText(self.controller.identity.display_name)
        self.connection_info_labels["software"].setText(self.controller.identity.software_version)
        self.connection_info_labels["serial"].setText(self.controller.identity.serial_number)
        self.connection_info_labels["mac"].setText(self.controller.identity.mac_address)

    def _refresh_battery_page(self) -> None:
        snapshot = self.controller.battery_status
        self.direction_badge.set_badge(f"方向: {snapshot.current_direction_text}", self._direction_tone(snapshot))
        self.source_badge.set_badge(f"数据源: {snapshot.source_title}", self._source_tone(snapshot))
        self.battery_updated_label.setText(f"更新时间：{snapshot.updated_at_text}")
        self.battery_source_note.setText(snapshot.source_note)

        metrics = {
            "pack_voltage": (snapshot.pack_voltage_text, snapshot.pack_voltage_detail_text, "#0b6b53"),
            "pack_current": (snapshot.current_text, snapshot.current_direction_text, self._direction_color(snapshot)),
            "soc": (snapshot.soc_text, f"协议版本 {snapshot.protocol_version}" if snapshot.supports_realtime_window else "旧布局 `SocElement.u16Soc`", "#b45309"),
            "max_temp": (snapshot.max_temp_text, f"raw {snapshot.max_temp_raw}", "#dc2626"),
            "min_temp": (snapshot.min_temp_text, f"raw {snapshot.min_temp_raw}", "#2563eb"),
            "mos_temp": (snapshot.mos_temp_text, f"raw {snapshot.mos_temp_raw}", "#ea580c"),
            "cell_max": (snapshot.max_cell_voltage_text, snapshot.max_cell_position_text, "#db2777"),
            "cell_min": (snapshot.min_cell_voltage_text, snapshot.min_cell_position_text, "#059669"),
            "cell_delta": (snapshot.cell_delta_text, "单体压差", "#7c3aed"),
            "soh": (snapshot.soh_text, "健康度", "#0f766e"),
            "cycle_count": (snapshot.cycle_count_text, "循环次数", "#4338ca"),
            "capacity_now": (snapshot.capacity_now_text, "当前剩余", "#0f766e"),
            "capacity_full": (snapshot.capacity_full_text, "当前满充", "#15803d"),
            "capacity_factory": (snapshot.capacity_factory_text, "出厂额定", "#92400e"),
        }
        for key, (value, detail, color) in metrics.items():
            self.metric_cards[key].set_content(value, detail, color)

        cell_values = [item.millivolts for item in snapshot.cell_voltages]
        cell_min = min(cell_values) if cell_values else 0
        cell_max = max(cell_values) if cell_values else 0
        for index, card in enumerate(self.cell_cards):
            if index < len(snapshot.cell_voltages):
                sample = snapshot.cell_voltages[index]
                if sample.millivolts == cell_max and sample.millivolts > 0:
                    color = "#db2777"
                elif sample.millivolts == cell_min and sample.millivolts > 0:
                    color = "#059669"
                else:
                    color = "#0b6b53"
                card.set_content(sample.voltage_text, sample.detail_text, color)
            else:
                card.set_content("—", "无数据", "#6b7280")

        self.system_status_label.setText(f"SystemStatus: {snapshot.system_status_hex_text}")
        self.active_flag_count_label.setText(f"活动标志数: {len(snapshot.active_status_flags)}")
        for flag in snapshot.status_flags:
            badge = self.flag_badges.get(flag.key)
            if badge is not None:
                badge.set_badge(flag.title, "success" if flag.is_active else "neutral")

        self.pack_voltage_mirror_label.setText(snapshot.pack_voltage_detail_text)
        self.battery_temp_adc_label.setText(snapshot.legacy_battery_temp_adc_text)
        self.mos_temp_adc_label.setText(snapshot.legacy_mos_temp_adc_text)

    def _refresh_block_views(self) -> None:
        self.battery_snapshot_edit.setPlainText(self._format_blocks(self.controller.battery_blocks()))
        self.debug_blocks_edit.setPlainText(self._format_blocks(self.controller.debug_blocks()))

    def _refresh_response_preview(self) -> None:
        self.response_preview_edit.setPlainText(self.controller.response_preview or "暂无响应")

    def _refresh_logs(self) -> None:
        self.log_count_label.setText(f"最近 {len(self.controller.logs)} 条")
        self.logs_table.setRowCount(len(self.controller.logs))
        for row, entry in enumerate(self.controller.logs):
            values = [
                entry.timestamp_text,
                entry.direction.value,
                entry.title,
                entry.payload_hex,
                entry.note,
            ]
            for column, value in enumerate(values):
                item = QTableWidgetItem(value)
                self.logs_table.setItem(row, column, item)

    def _on_device_selection_changed(self) -> None:
        item = self.device_tree.currentItem()
        if item is None:
            self.controller.set_selected_device(None)
            return
        device_id = item.data(0, Qt.ItemDataRole.UserRole)
        self.controller.set_selected_device(device_id)

    def _handle_manual_read(self) -> None:
        self.controller.manual_read_address = self.manual_read_address_edit.text().strip()
        self.controller.manual_read_quantity = self.manual_read_quantity_edit.text().strip()
        self._run_action(self.controller.read_manual_block)

    def _handle_manual_write(self) -> None:
        self.controller.manual_write_address = self.manual_write_address_edit.text().strip()
        self.controller.manual_write_words = self.manual_write_words_edit.text().strip()
        self._run_action(self.controller.write_manual_words)

    def _handle_quick_soc(self) -> None:
        self.controller.quick_soc_value = self.quick_soc_edit.text().strip()
        self._run_action(self.controller.write_soc_value)

    def _handle_raw_send(self) -> None:
        self.controller.raw_hex_command = self.raw_frame_edit.toPlainText().strip()
        self._run_action(self.controller.send_raw_command)

    def _handle_bt_name_write(self) -> None:
        self.controller.bt_name_suffix = self.bt_name_edit.text().strip()
        self._run_action(self.controller.write_bluetooth_name_suffix)

    def _run_action(self, action: callable) -> None:
        try:
            action()
        except Exception as exc:
            self.controller.report_external_error(str(exc))

    def _on_auto_refresh_toggled(self) -> None:
        self._update_auto_refresh_state()
        if (
            self.auto_refresh_checkbox.isChecked()
            and self.tab_widget.currentIndex() == 0
            and self.controller.can_send_commands
        ):
            self._run_action(self.controller.refresh_battery_status)

    def _update_auto_refresh_state(self) -> None:
        enabled = (
            self.tab_widget.currentIndex() == 0
            and self.auto_refresh_checkbox.isChecked()
            and self.controller.can_send_commands
        )
        if enabled:
            interval = int(self.refresh_interval_combo.currentData())
            if self._battery_refresh_timer.interval() != interval or not self._battery_refresh_timer.isActive():
                self._battery_refresh_timer.start(interval)
        else:
            self._battery_refresh_timer.stop()

    def _on_battery_refresh_timeout(self) -> None:
        if self.controller.can_send_commands:
            self._run_action(self.controller.refresh_battery_status)

    def _default_export_dir(self) -> Path:
        documents_dir = Path.home() / "Documents" / "BMSAssistantQt"
        try:
            documents_dir.mkdir(parents=True, exist_ok=True)
            return documents_dir
        except Exception:
            return Path.home()

    def _export_logs(self) -> None:
        if not self.controller.logs:
            self.controller.report_external_error("当前没有可导出的报文日志")
            return

        default_path = self._default_export_dir() / f"BMSAssistantQt-logs-{datetime.now():%Y%m%d-%H%M%S}.csv"
        path, _ = QFileDialog.getSaveFileName(self, "导出报文日志", str(default_path), "CSV Files (*.csv)")
        if not path:
            return

        try:
            with open(path, "w", newline="", encoding="utf-8-sig") as handle:
                writer = csv.writer(handle)
                writer.writerow(["时间", "方向", "标题", "Payload", "说明"])
                for entry in self.controller.logs:
                    writer.writerow(
                        [
                            entry.timestamp.isoformat(timespec="seconds"),
                            entry.direction.value,
                            entry.title,
                            entry.payload_hex,
                            entry.note,
                        ]
                    )
        except Exception as exc:
            self.controller.report_external_error(f"导出报文日志失败: {exc}")
            return

        self.controller.status_message = f"报文日志已导出: {path}"
        self.controller.statusChanged.emit()

    def _export_battery_snapshot(self) -> None:
        snapshot = self.controller.battery_status
        if not snapshot.is_supported and not self.controller.battery_blocks():
            self.controller.report_external_error("当前没有可导出的电池快照，请先刷新一次电池状态")
            return

        default_path = self._default_export_dir() / f"BMSAssistantQt-battery-{datetime.now():%Y%m%d-%H%M%S}.json"
        path, _ = QFileDialog.getSaveFileName(self, "导出电池快照", str(default_path), "JSON Files (*.json)")
        if not path:
            return

        payload = {
            "exportedAt": datetime.now().isoformat(timespec="seconds"),
            "connectedDevice": self.controller.connected_device_name,
            "identity": asdict(self.controller.identity),
            "batteryStatus": asdict(snapshot),
            "batteryBlocks": [self._serialize_block(block) for block in self.controller.battery_blocks()],
            "responsePreview": self.controller.response_preview,
        }

        try:
            with open(path, "w", encoding="utf-8") as handle:
                json.dump(payload, handle, ensure_ascii=False, indent=2, default=self._json_default)
        except Exception as exc:
            self.controller.report_external_error(f"导出电池快照失败: {exc}")
            return

        self.controller.status_message = f"电池快照已导出: {path}"
        self.controller.statusChanged.emit()

    def _format_blocks(self, blocks: Iterable[RegisterBlock]) -> str:
        items = list(blocks)
        if not items:
            return "尚无寄存器块。"

        sections: list[str] = []
        for block in items:
            sections.append(
                "\n".join(
                    [
                        f"[{block.title}] 起始 {block.start_address_text} 更新时间 {block.updated_at_text}",
                        block.word_lines(),
                        f"响应: {block.response_hex}",
                    ]
                )
            )
        return "\n\n".join(sections)

    def _bluetooth_tone(self) -> str:
        if self.controller.bluetooth_state_label in {"已开启", "系统托管"}:
            return "success"
        if self.controller.bluetooth_state_label == "已关闭":
            return "warning"
        return "info"

    def _connection_tone(self) -> str:
        if self.controller.connection_status is ConnectionStatus.READY:
            return "success"
        if self.controller.connection_status in {ConnectionStatus.CONNECTING, ConnectionStatus.CONNECTED, ConnectionStatus.SCANNING}:
            return "info"
        if self.controller.connection_status is ConnectionStatus.FAILED:
            return "danger"
        return "neutral"

    def _direction_tone(self, snapshot: BatteryStatusSnapshot) -> str:
        if snapshot.signed_current_raw > 0:
            return "success"
        if snapshot.signed_current_raw < 0:
            return "warning"
        return "neutral"

    def _source_tone(self, snapshot: BatteryStatusSnapshot) -> str:
        if snapshot.source.value == "实时窗口模式":
            return "success"
        if snapshot.source.value == "旧寄存器兼容模式":
            return "info"
        return "neutral"

    def _direction_color(self, snapshot: BatteryStatusSnapshot) -> str:
        if snapshot.signed_current_raw > 0:
            return "#15803d"
        if snapshot.signed_current_raw < 0:
            return "#b45309"
        return "#475569"

    def _serialize_block(self, block: RegisterBlock) -> dict[str, object]:
        return {
            "title": block.title,
            "startAddress": block.start_address_text,
            "startAddressRaw": block.start_address,
            "updatedAt": block.updated_at,
            "words": list(block.words),
            "responseHex": block.response_hex,
        }

    def _json_default(self, value: object) -> object:
        if isinstance(value, datetime):
            return value.isoformat(timespec="seconds")
        if hasattr(value, "value"):
            return getattr(value, "value")
        return str(value)

    def _restore_settings(self) -> None:
        geometry = self._settings.value("window/geometry")
        if geometry is not None:
            self.restoreGeometry(geometry)

        scan_mode = self._settings.value("scan/mode", self.controller.scan_mode.value, type=str)
        try:
            self.controller.scan_mode = ScanMode(scan_mode)
        except Exception:
            self.controller.scan_mode = ScanMode.ALL_DEVICES

        self.controller.search_text = self._settings.value("scan/search_text", "", type=str)
        self.controller.show_only_likely_bms = self._settings.value("scan/show_only_likely_bms", False, type=bool)

        self.controller.manual_read_address = self._settings.value("debug/manual_read_address", self.controller.manual_read_address, type=str)
        self.controller.manual_read_quantity = self._settings.value("debug/manual_read_quantity", self.controller.manual_read_quantity, type=str)
        self.controller.manual_write_address = self._settings.value("debug/manual_write_address", self.controller.manual_write_address, type=str)
        self.controller.manual_write_words = self._settings.value("debug/manual_write_words", self.controller.manual_write_words, type=str)
        self.controller.quick_soc_value = self._settings.value("debug/quick_soc_value", self.controller.quick_soc_value, type=str)
        self.controller.raw_hex_command = self._settings.value("debug/raw_hex_command", self.controller.raw_hex_command, type=str)
        self.controller.bt_name_suffix = self._settings.value("debug/bt_name_suffix", self.controller.bt_name_suffix, type=str)

        self.auto_refresh_checkbox.setChecked(self._settings.value("battery/auto_refresh_enabled", True, type=bool))
        self._set_combo_current_data(
            self.refresh_interval_combo,
            self._settings.value("battery/refresh_interval_ms", 2000, type=int),
        )

        tab_index = self._settings.value("ui/current_tab_index", 0, type=int)
        if 0 <= tab_index < self.tab_widget.count():
            self.tab_widget.setCurrentIndex(tab_index)

    def _save_settings(self) -> None:
        self._settings.setValue("window/geometry", self.saveGeometry())
        self._settings.setValue("ui/current_tab_index", self.tab_widget.currentIndex())

        self._settings.setValue("scan/mode", self.scan_mode_combo.currentText())
        self._settings.setValue("scan/search_text", self.search_input.text())
        self._settings.setValue("scan/show_only_likely_bms", self.only_likely_checkbox.isChecked())

        self._settings.setValue("battery/auto_refresh_enabled", self.auto_refresh_checkbox.isChecked())
        self._settings.setValue("battery/refresh_interval_ms", int(self.refresh_interval_combo.currentData()))

        self._settings.setValue("debug/manual_read_address", self.manual_read_address_edit.text().strip())
        self._settings.setValue("debug/manual_read_quantity", self.manual_read_quantity_edit.text().strip())
        self._settings.setValue("debug/manual_write_address", self.manual_write_address_edit.text().strip())
        self._settings.setValue("debug/manual_write_words", self.manual_write_words_edit.text().strip())
        self._settings.setValue("debug/quick_soc_value", self.quick_soc_edit.text().strip())
        self._settings.setValue("debug/raw_hex_command", self.raw_frame_edit.toPlainText().strip())
        self._settings.setValue("debug/bt_name_suffix", self.bt_name_edit.text().strip())
        self._settings.sync()

    def _set_combo_current_data(self, combo: QComboBox, expected_value: int) -> None:
        for index in range(combo.count()):
            if combo.itemData(index) == expected_value:
                combo.setCurrentIndex(index)
                return

    def closeEvent(self, event: QCloseEvent) -> None:
        self._save_settings()
        super().closeEvent(event)

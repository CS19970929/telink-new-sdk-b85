
"""Replay the legacy CAN maintenance window for legacy BMS and C073."""
from pathlib import Path
from types import SimpleNamespace
import os
import struct
import tempfile
import zlib

import comm_tool_upgrade_ui as ui


class Replay:
    def __init__(self, image, adapter, identity=None, valid_image=True):
        self.image = image
        self.version = adapter
        self.identity = image.running_identity() if identity is None else identity
        self.valid_image = valid_image
        self.commands = []
        self.loaded = bytearray()
        self.cache = bytes(15)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False

    def command(self, op, payload=b"", **_):
        self.commands.append(op)
        data = b""
        if op == ui.CMD_GET_INFO:
            data = struct.pack("<4B4I", 1, *self.version, 250000, 0x08018000, 0x67800, 0)
        elif op == ui.CMD_FW_INFO:
            data = self.cache
        elif op == ui.CMD_FW_BEGIN:
            address, size, crc16, crc = struct.unpack("<IIHI", payload)
            assert (address, size, crc16, crc) == (self.image.address, len(self.image.data),
                                               ui.crc16_modbus(self.image.data), self.image.crc32)
            self.loaded.clear()
        elif op == ui.CMD_FW_DATA:
            offset, = struct.unpack_from("<I", payload)
            assert offset == len(self.loaded)
            if self.image.board:
                assert len(payload[4:]) <= ui.C073_CACHE_CHUNK_BYTES
            self.loaded.extend(payload[4:])
        elif op == ui.CMD_FW_END:
            size, crc16, crc = struct.unpack("<IHI", payload)
            assert self.loaded == self.image.data
            self.cache = struct.pack("<IIHIB", self.image.address, size, crc16, crc, 1)
        elif op == ui.CMD_BMS_READ:
            addr, count = struct.unpack("<HH", payload)
            if (addr, count) == (0xD170, 12):
                assert self.image.board
                data = struct.pack("<12H", *self.identity)
            elif (addr, count) == (0xD17C, 8):
                assert self.image.board
                version, size, crc = self.image.version, len(self.image.data), self.image.crc32
                data = struct.pack("<8H", version >> 16, version & 0xFFFF, size >> 16, size & 0xFFFF,
                                   crc >> 16, crc & 0xFFFF, int(self.valid_image), 0)
            else:
                assert (addr, count) == (0xD034, 2)
                data = struct.pack("<HH", 67, 100)
        elif op == ui.CMD_UPGRADE:
            assert self.cache[14] == 1
        elif op == ui.CMD_UPGRADE_STATUS:
            data = struct.pack("<3BIIH", 2, 100, 0, len(self.image.data), len(self.image.data), len(self.image.data)//8)
        return SimpleNamespace(payload=data)


def window(path, replay):
    obj = object.__new__(ui.UpgradeUi)
    obj.active_bin = path
    obj.active_baud = 115200
    obj.active_can_bitrate, obj.active_node_id, obj.active_app_can_addr = 250000, 1, 0
    obj._open_client = lambda: replay
    obj._require_comm_tool_client = lambda client, label: client
    obj._set_can_target = lambda client, force=False: None
    obj._emit = lambda *args: None
    return obj


def reject(action, expected):
    try:
        action()
    except (ValueError, RuntimeError) as exc:
        assert expected in str(exc), str(exc)
    else:
        raise AssertionError(f"应当拒绝：{expected}")


def main():
    with tempfile.TemporaryDirectory(prefix="can-maintenance-replay-") as folder:
        root = Path(folder)
        old = root / "legacy.bin"
        old.write_bytes(struct.pack("<II", 0x20004000, ui.APP_BASE_ADDR+9) + bytes(1016))
        c073 = root / "c073.bin"
        raw = bytearray(1024)
        struct.pack_into("<II", raw, 0, 0x20004000, ui.C073_APP_BASE+0x121)
        struct.pack_into("<4I", raw, ui.C073_ID_OFFSET, ui.C073_GUARDED_BOARD, ui.C073_APP_BASE, 0x20001, 1008)
        c073.write_bytes(raw)
        image = ui.load_can_upgrade_image(c073)
        pkg = root / "c073.c073"
        head = struct.pack("<IHH5I", ui.C073_PACKAGE_MAGIC, 2, 32, image.board, image.address,
                           len(image.data), image.version, image.crc32)
        pkg.write_bytes(head + struct.pack("<I", zlib.crc32(head)) + image.data)
        assert ui.load_can_upgrade_image(pkg) == image
        damaged = root / "bad.c073"
        damaged.write_bytes(pkg.read_bytes()[:-1] + b"\x01")
        reject(lambda: ui.load_can_upgrade_image(damaged), "CRC32")
        ui.time.sleep = lambda *_: None
        for adapter in ((0, 2, 4), (0, 2, 11)):
            legacy = Replay(ui.load_can_upgrade_image(old), adapter)
            window(old, legacy)._worker_upgrade_selected()
            assert ui.CMD_FW_BEGIN in legacy.commands and ui.CMD_UPGRADE in legacy.commands
            assert legacy.commands.count(ui.CMD_BMS_READ) == 1
        new = Replay(image, (0, 2, 11))
        obj = window(pkg, new)
        obj._worker_upgrade_selected()
        assert new.commands.count(ui.CMD_FW_BEGIN) == 1 and new.commands.count(ui.CMD_UPGRADE) == 1
        obj._worker_upgrade_selected()
        assert new.commands.count(ui.CMD_FW_BEGIN) == 1 and new.commands.count(ui.CMD_UPGRADE) == 2
        old_adapter = Replay(image, (0, 2, 4))
        reject(window(pkg, old_adapter)._worker_upgrade_selected, "需要 0.2.11")
        assert set(old_adapter.commands) == {ui.CMD_GET_INFO}
        wrong = image.running_identity()
        wrong[3] = ui.C073_LEGACY_BOARD & 0xFFFF
        wrong_target = Replay(image, (0, 2, 11), identity=wrong)
        reject(window(pkg, wrong_target)._worker_upgrade_selected, "不匹配")
        assert ui.CMD_FW_BEGIN not in wrong_target.commands and ui.CMD_UPGRADE not in wrong_target.commands
        invalid = Replay(image, (0, 2, 11), valid_image=False)
        reject(window(pkg, invalid)._worker_upgrade_selected, "不一致")
    print("PASS 旧版和新版 Comm Tool 的旧项目流程、C073 缓存复用、版本门槛、错板拒绝和重启校验")


if __name__ == "__main__":
    main()

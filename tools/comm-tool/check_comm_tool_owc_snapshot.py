"""Replay PC snapshots without serial access, C compilation or firmware output."""
import unittest
from types import SimpleNamespace

from comm_tool_owc import SNAPSHOT_HEADER, decode_snapshot, read_snapshots, visible_fields


def payload(version=1, raw=b"", session=0, valid=1, age=100):
    # v1 CtOwc_EncodeSnapshot: identical 32-byte layout; offset 30 is reserved zero.
    return SNAPSHOT_HEADER.pack(version, 1, bool(raw), valid if raw else 0,
                                age, 10, 0, 0, 0, 10, len(raw), session) + raw


def packet(packet_id, data):
    raw = bytes([packet_id, 0x13, len(data)]) + data
    return raw + bytes([sum(raw) & 255])


class SnapshotTests(unittest.TestCase):
    def test_existing_bridge_realtime(self):
        data = bytearray(29)
        data[0:5] = bytes([160, 8, 2, 236, 19])  # SOC80%, 52V, +10A
        raw = packet(0x3A, data)
        for version, session in ((1, 0), (2, 7)):
            with self.subTest(version=version):
                snapshot = decode_snapshot(payload(version, raw, session), 0x3A)
                fields = {k: v for k, v, _ in visible_fields(snapshot, {0x3A: snapshot})}
                self.assertEqual(fields["Voltage"], "52")
                self.assertEqual(fields["Current"], "10")
                self.assertEqual(snapshot.session, session)
                self.assertEqual(snapshot.snapshot_version, version)

    def test_existing_bridge_all_queries(self):
        raw = packet(0x3C, b"BMS-123")

        class Client:
            def command(self, command, request):
                self_test.assertEqual(command, 0x44)
                return SimpleNamespace(payload=payload(raw=raw if request[0] in (0, 0x3C) else b""))

        self_test = self
        snapshots = read_snapshots(Client())
        fields = {k: v for k, v, _ in visible_fields(snapshots[0x3C], snapshots)}
        self.assertEqual(fields["Battery code"], "BMS-123")

    def test_reject_mixed_snapshot_versions(self):
        class Client:
            def command(self, command, request):
                return SimpleNamespace(payload=payload(1 if request[0] == 1 else 2))

        with self.assertRaises(ValueError):
            read_snapshots(Client())

    def test_reject_invalid_headers_and_checksum(self):
        raw = packet(0x3C, b"BMS-123")
        for bad in (payload(3), payload(1, raw, 4), payload(2, raw, 0),
                    payload(1, raw[:-1] + bytes([raw[-1] ^ 1])), payload(1, raw)[:-1]):
            with self.subTest(payload=bad):
                with self.assertRaises(ValueError):
                    decode_snapshot(bad)
        snapshot = decode_snapshot(payload(1, raw, valid=0)[:-1] + bytes([raw[-1] ^ 1]))
        self.assertFalse(visible_fields(snapshot, {0: snapshot}))
        stale = decode_snapshot(payload(1, raw, age=80000))
        self.assertFalse(visible_fields(stale, {0x3C: stale}))


if __name__ == "__main__":
    unittest.main()

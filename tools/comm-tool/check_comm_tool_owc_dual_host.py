"""双协议PC协商、快照和维护窗口回归，不访问串口。"""
import struct
import threading
import unittest
from dataclasses import replace
from types import SimpleNamespace
from comm_tool_host import CommToolCommandError
from comm_tool_owc import SNAPSHOT_HEADER, decode_snapshot, visible_fields, link_online, read_snapshots, start_receiver, control_receiver

RAW=bytes([0,80,76,236,0,136])
def payload(raw=RAW,protocol=2,mode=0,session=9,age=100,mismatch=0):
    return SNAPSHOT_HEADER.pack(3,1,bool(raw),bool(raw),age,2,0,0,0,2,len(raw),session)+bytes([mode,protocol,protocol if raw else 0,mismatch])+raw

class DualHostTests(unittest.TestCase):
    def test_fields_and_expiry(self):
        live=decode_snapshot(payload(),0x80,True)
        fields=dict((k,v) for k,v,_ in visible_fields(live,{0:live,0x80:live}))
        self.assertEqual((fields["SOC"],fields["Voltage"],fields["Battery temperature"]),("80","76","-20"))
        self.assertNotIn("Current",fields)
        stale=replace(live,age_ms=5001)
        self.assertFalse(link_online({0:stale}))
        self.assertEqual(visible_fields(stale,{0:stale}),[])
        mismatch=replace(live,mismatch_protocol=1)
        self.assertFalse(link_online({0:mismatch}))
        self.assertEqual(visible_fields(mismatch,{0:mismatch}),[])

    def test_reject_malformed(self):
        for p,selector,extended in ((payload(),0,False),(payload(),0x3a,True),
            (payload(mode=3),0,True),(payload(protocol=0),0,True),
            (payload()[:-1],0,True),(payload()[:34],0,True)):
            with self.subTest(selector=selector,extended=extended):
                with self.assertRaises(ValueError):decode_snapshot(p,selector,extended)
        empty=decode_snapshot(payload(raw=b"",protocol=0),0,True)
        self.assertFalse(link_online({0:empty}))

    def test_modern_negotiation(self):
        commands=[]
        class Client:
            def command(self,cmd,p=b""):
                commands.append((cmd,p))
                if cmd==0x45:return SimpleNamespace(payload=bytes([1,7,3,0,36,0,3,1]))
                if cmd==0x46:return SimpleNamespace(payload=b"")
                if cmd==0x47:return SimpleNamespace(payload=payload(raw=RAW if p[0] in (0,0x80) else b"",mode=2))
                raise AssertionError(cmd)
        client=Client()
        self.assertEqual(start_receiver(client,2),(True,2))
        self.assertEqual(commands[1],(0x46,bytes([1,2])))
        snapshots=read_snapshots(client,True)
        self.assertEqual(len(snapshots),6)
        control_receiver(client,False,2,True)
        self.assertEqual(commands[-1],(0x46,bytes([0,2])))

    def test_legacy_fallback(self):
        commands=[]
        class Client:
            def command(self,cmd,p=b""):
                commands.append((cmd,p))
                if cmd==0x45:raise CommToolCommandError(2)
                return SimpleNamespace(payload=b"")
        self.assertEqual(start_receiver(Client(),0),(False,1))
        self.assertEqual(commands[-1],(0x43,b"\x01"))
        commands.clear()
        with self.assertRaises(ValueError):start_receiver(Client(),2)
        self.assertEqual(len(commands),1)

    def test_errors_do_not_fall_back(self):
        class Client:
            def command(self,cmd,p=b""):raise TimeoutError()
        with self.assertRaises(TimeoutError):start_receiver(Client())
        class Bad:
            def command(self,cmd,p=b""):return SimpleNamespace(payload=b"\x01")
        with self.assertRaises(ValueError):start_receiver(Bad())
        class NoPins:
            def command(self,cmd,p=b""):return SimpleNamespace(payload=bytes([1,0,3,0,36,0,3,1]))
        with self.assertRaises(ValueError):start_receiver(NoPins())

    def test_cross_session_read(self):
        class Client:
            i=0
            def command(self,cmd,p):
                self.i+=1
                return SimpleNamespace(payload=payload(raw=RAW if p[0] in (0,0x80) else b"",
                    session=self.i))
        with self.assertRaises(ValueError):read_snapshots(Client(),True)

    def test_maintenance_window(self):
        import comm_tool_upgrade_ui as ui
        parent=ui.UpgradeUi("",19200,ui.DEFAULT_BIN)
        parent.withdraw()
        window=ui.OwcMonitorWindow(parent,"TEST")
        window.withdraw()
        try:
            self.assertEqual(window.mode_box.current(),0)
            live=decode_snapshot(payload(),0x80,True)
            window.snapshots={0:live,0x80:live}
            window._render()
            rows=[window.tree.item(i,"values") for i in window.tree.get_children()]
            self.assertTrue(any(r[1]=="电池温度" and r[2]=="-20" for r in rows))
            self.assertFalse(any(r[1]=="电流" for r in rows))
            window.thread=SimpleNamespace(is_alive=lambda:True)
            window._mode_changed()
            self.assertTrue(window.stop_event.is_set() and window.restart_pending)
            self.assertFalse(window.tree.get_children())
            window._stop()
            self.assertFalse(window.restart_pending)
            window.thread=None
        finally:
            window.destroy();parent.destroy()

if __name__=="__main__":
    unittest.main()

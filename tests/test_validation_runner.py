"""检验验证工具不会把漏跑、超时、源码变化或空选择包装成绿色。"""
import os
from pathlib import Path
import sys
import tempfile
import unittest
import subprocess
from unittest.mock import patch
import run_host_regression as runner
from validation_support import ScenarioFailure, run_c
from d014_reset_boundary_probe import policy_gap


class RunnerTests(unittest.TestCase):
    def test_runtime_failure_preserves_child_evidence(self):
        result=subprocess.CompletedProcess(['child'],7,'observed\n','assertion\n')
        with patch('validation_support.subprocess.run',side_effect=[None,result]):
            with self.assertRaises(ScenarioFailure) as caught:
                run_c('int main(void){return 7;}')
        self.assertEqual((caught.exception.returncode,caught.exception.stdout,caught.exception.stderr),
                         (7,'observed\n','assertion\n'))

    def test_reset_probe_does_not_label_crashes_as_policy_gaps(self):
        stdout='RESET_OBSERVATION direction=charge history=durable recorded=1 first_on_sample=8 current_a10=0\n'
        stderr='RESET_POLICY_GAP direction=charge history=durable expected=OFF_UNTIL_PHYSICAL_RELEASE actual=ON\n'
        error=ScenarioFailure('child failed',subprocess.CompletedProcess(['child'],1,stdout,stderr))
        self.assertEqual(policy_gap(error,'charge','durable'),8)
        for code,out,err in ((139,stdout,stderr),(1,'',stderr),(1,stdout,stderr+'AddressSanitizer error'),
                             (1,stdout.replace('recorded=1','recorded=0'),stderr),(1,stdout,'assertion failed')):
            with self.subTest(code=code,out=out,err=err):
                invalid=ScenarioFailure('child failed',subprocess.CompletedProcess(['child'],code,out,err))
                with self.assertRaises(ScenarioFailure):policy_gap(invalid,'charge','durable')

    def test_catalog_is_complete_and_common_tests_have_four_products(self):
        commands=runner.commands_for(runner.PRODUCTS,None)
        self.assertGreater(len(commands),100)
        for name in ('storage_host_check','protection_scenarios_host_check','soc_scenarios_host_check'):
            self.assertEqual({c[1] for c in commands if c[0]==name},set(runner.PRODUCTS))

    def test_empty_selection_is_an_error(self):
        with self.assertRaises(ValueError):runner.commands_for(('d008',),['sh_register_scenarios_host_check'])
        with self.assertRaises(ValueError):runner.commands_for(runner.PRODUCTS,['misspelled_test'])

    def test_failure_keeps_diagnostics(self):
        code,output,_=runner.execute([sys.executable,'-c','print("expected=1 actual=2");raise SystemExit(7)'],os.environ.copy(),5)
        self.assertEqual(code,7);self.assertIn(b'expected=1 actual=2',output)

    def test_timeout_is_red_and_keeps_partial_output(self):
        code,output,_=runner.execute([sys.executable,'-c','import time;print("before timeout",flush=True);time.sleep(30)'],os.environ.copy(),0.5)
        self.assertEqual(code,124);self.assertIn(b'before timeout',output)

    def test_source_fingerprint_uses_bytes_not_mtime(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'tests').mkdir();(root/'CMakeLists.txt').write_text('')
            for product in runner.PRODUCTS:
                p=root/'bms/products'/product;p.mkdir(parents=True);(p/'sources.txt').write_text('')
            p=root/'tests/new_check.py';p.write_text('before');timestamp=p.stat().st_mtime_ns
            with patch.object(runner,'ROOT',root):
                a=runner.fingerprint();p.write_text('after');os.utime(p,ns=(timestamp,timestamp));b=runner.fingerprint()
            self.assertNotEqual(a['sha256'],b['sha256'])
            self.assertIn('tests/new_check.py',a['files'])

    def test_behavior_changes_are_field_specific(self):
        result=runner.differences({'x':[{'threshold':100}]},{'x':[{'threshold':101}]})
        self.assertEqual(result,[{'field':'/x/0/threshold','before':100,'after':101}])

    def test_product_divergence_is_red(self):
        rows=[{'check':'protection_scenarios_host_check','product':p,'exit':0,
               'observations':[{'digest':v}]} for p,v in (('d008','a'),('d014','b'))]
        self.assertTrue(runner.consistency_errors(rows))
        rows[1]['observations']=rows[0]['observations']
        self.assertEqual(runner.consistency_errors(rows),[])

    def test_integrity_failure_reaches_junit(self):
        import xml.etree.ElementTree as ET
        report={'results':[],'git':{'head':'test'},'inputs':{'sha256':'test'},'verdict':'失败',
                'planned':1,'behavior_changes':[],'source_changed_during_run':True,
                'integrity_errors':['运行期间源码改变']}
        with tempfile.TemporaryDirectory() as folder:
            runner.write_reports(Path(folder),report)
            self.assertEqual(ET.parse(Path(folder)/'junit.xml').getroot().get('failures'),'1')


if __name__=='__main__':unittest.main()

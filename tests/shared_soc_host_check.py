"""同一 SOC 长时场景走各产品实际 app 样本入口；存储/环境仍为显式替身。"""
import subprocess
import sys
from validation_support import ROOT, evidence

subprocess.run([sys.executable, str(ROOT/'tests/d008_power_soc_host_check.py'), '--soc-only'], check=True)
evidence({'domain': 'soc', 'virtual_days': [30, 90], 'capacity_fixture_ah': 100,
          'boundary': '生产 SOC 全函数体与所选 app 样本函数；100Ah 共同比较场景，非产品默认容量精度签核'})

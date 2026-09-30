"""Resolve existing test references to the actual monorepo source files.

This is a path lookup only: it never creates or synchronizes source copies.
"""
import os
from pathlib import Path

class Sources:
    def __init__(self, root):
        self.root=Path(root)
        self.product=os.environ.get("BMS_PRODUCT", "d014")
    def __truediv__(self, name):
        name=str(name)
        if name=="bms_board.c": return self.root/"bms/platform/telink/bms_board.c"
        product=self.root/"bms/products"/self.product/name
        if product.exists(): return product
        candidates=[x for x in (self.root/"bms").rglob(name) if "products" not in x.parts]
        if len(candidates)==0: return self.root/"bms/core"/name
        if len(candidates)!=1: raise FileNotFoundError(f"ambiguous/missing shared source {name}: {candidates}")
        return candidates[0]
    def __str__(self): return str(self.root/"bms/core")
    def __fspath__(self): return str(self)


def host_includes(root, product=None):
    root=Path(root);product=product or os.environ.get("BMS_PRODUCT", "d014")
    dirs=[root,root/"bms/core", root/"bms/app",root/"bms/platform/telink",root/"bms/products"/product,
          root/"bms/afe"/("dvc1124" if product=="d008" else "sh3673510"),
          root/"tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk",
          root/"tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/drivers/B85",
          root/"tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/common"]
    return [part for directory in dirs for part in ("-I",str(directory))]

def selected_source(path, product=None):
    """Read only this unit's actual TC32-preprocessed branch, outside the repository."""
    import subprocess,re,shutil,shlex
    product=product or os.environ.get("BMS_PRODUCT", "d014")
    path=Path(path);root=path.parents[2] if path.parts[-3]=="bms" else next(parent for parent in path.parents if (parent/"bms_tools").exists())
    target_cc=Path(os.environ.get("TC32_CC", "C:/TelinkIoTStudio/opt/tc32/bin/tc32-elf-gcc.exe"))
    compiler=[str(target_cc)] if target_cc.is_file() else shlex.split(os.environ.get("CC",shutil.which("gcc") or "cc"))
    result=subprocess.run([*compiler,"-E","-dD","-fdirectives-only",
        "-D__PROJECT_8258_BLE_SAMPLE__=1","-DCHIP_TYPE=CHIP_TYPE_825x",*host_includes(root,product),
        "-I",str(root/"tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/common"),str(path)],
        capture_output=True,check=True)
    active=False;out=[];target=path.resolve().as_posix().casefold()
    for line in result.stdout.decode("utf8",errors="replace").splitlines(True):
        marker=re.match(r'^# \d+ "(.*?)"',line)
        if marker:active=Path(marker[1].replace("\\","/")).resolve().as_posix().casefold()==target
        elif active:out.append(line)
    return "".join(out).replace("\r\n", "\n")

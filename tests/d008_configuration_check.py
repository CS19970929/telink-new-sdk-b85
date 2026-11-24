"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_d008_20s_profile_contract_check():
    print("CHECK d008_20s_profile_contract_check", flush=True)
    #!/usr/bin/env python3
    """D008 20S-NMC compile-profile and DVC channel-mask safety contracts."""
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import re

    ROOT = Path(__file__).resolve().parents[1]
    HERE = Sources(ROOT)
    profile = (HERE / 'd008_product_profile.h').read_text(encoding='utf-8')
    dvc = (HERE / 'dvc1124.c').read_text(encoding='utf-8')
    cfg = (HERE / 'bms_product.h').read_text(encoding='utf-8') + (HERE / 'dvc1124_product_defaults.h').read_text(encoding='utf-8')
    boot = (HERE / 'dvc1124_boot.c').read_text(encoding='utf-8')
    store = dvc + boot
    service = (HERE / 'dvc1124_config_service.c').read_text(encoding='utf-8')

    assert '#define D008_PRODUCT_PROFILE_20S_NMC  2u' in profile
    block = profile.split('#elif (D008_PRODUCT_PROFILE == D008_PRODUCT_PROFILE_20S_NMC)', 1)[1].split('#else', 1)[0]
    assert '#define D008_PRODUCT_CELL_COUNT       20u' in block
    assert 'BMS_SOC_CHEMISTRY_NMC' in block
    assert '#define DVC1124_DEFAULT_CELL_COUNT           D008_PRODUCT_CELL_COUNT' in cfg
    assert 'DVC1124_SetCellCount((uint8_t)DVC1124_DEFAULT_CELL_COUNT)' in dvc

    # Reproduce the driver mask algorithm: for 20S, only channels 21..24 are masked.
    def masks(cell_count: int):
        out = [0, 0, 0]
        for cell in range(5, 25):
            if cell <= cell_count:
                continue
            if cell >= 17:
                out[0] |= 1 << (cell - 17)
            elif cell >= 9:
                out[1] |= 1 << (cell - 9)
            else:
                out[2] |= 1 << (cell - 1)
        return out
    assert masks(20) == [0xF0, 0x00, 0x00]
    assert masks(24) == [0x00, 0x00, 0x00]
    assert 'for (cell = 5u; cell <= DVC1124_MAX_CELLS; ++cell)' in dvc

    # D008 board invariants are compile-time owned and re-applied after every init.
    assert 'ConfigStoreLoad' not in store and 'ConfigStoreRestore' not in store
    assert 'DVC1124_DEFAULT_HIGH_SIDE_FET_MASK' in dvc
    assert 'DVC1124_EncodeCurrentWake(DVC1124_CURRENT_WAKE_THRESHOLD_UV' in store
    assert 'DVC1124_DEFAULT_DSG_MASK_POLICY' in store
    assert 'DVC1124_DEFAULT_CHG_MASK_POLICY' in store
    assert 'DVC1124_ApplyProjectOperatingConfig()' in store
    assert 's_project_config_pending' not in store
    assert 'DVC1124_CFG_ERR_READ_ONLY' in service
    assert 'DVC1124_ConfigStoreSave' not in service


    # Balancing is armed by request and renewed below the ~60s hardware auto-clear.
    assert '#define DVC_BALANCE_REFRESH_INTERVAL_MS 45000u' in dvc
    assert 'bms_afe_driver_set_balance_mask' in dvc

    print('D008 20S/product safety completion contract: PASS')

def check_d008_profile_selection_host_check():
    print("CHECK d008_profile_selection_host_check", flush=True)
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import tempfile,subprocess,os,shlex
    ROOT=Path(__file__).resolve().parents[1]
    APP = Sources(ROOT)
    with tempfile.TemporaryDirectory(prefix='d008-profile-') as d:
        p=Path(d)/'profile.c'
        import sys
        sys.path.insert(0, str(ROOT))
        from bms_tools import build_options
        configured = build_options.read_source(ROOT, 'd008')['cell_count']
        for selector,count in ((None,configured),(1,24),(2,20),(3,16),(5,23)):
            p.write_text('#include "d008_product_profile.h"\n#if D008_PRODUCT_CELL_COUNT != %d\n#error inconsistent_profile\n#endif\nint main(void){return 0;}\n'%count)
            subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99',*host_includes(ROOT),str(p),'-o',str(Path(d)/'profile.exe')]+([] if selector is None else ['-DD008_PRODUCT_PROFILE=%d'%selector]),check=True)
    print('PASS committed source default and explicit 24S/20S/16S/23S identities')

    with tempfile.TemporaryDirectory(prefix='d008-release-profile-') as d:
        p=Path(d)/'profile.c'
        p.write_text('#include "d008_product_profile.h"\n')
        base=shlex.split(os.environ.get('CC','cc'))+['-E','-DBMS_PRODUCTION_BUILD=1',*host_includes(ROOT),str(p)]
        assert subprocess.run(base,capture_output=True).returncode != 0
        for selector in (1,2,3,4,5):
            subprocess.run(base+['-DD008_PRODUCT_PROFILE=%d'%selector],check=True,stdout=subprocess.DEVNULL)
        p.write_text('#include "bms_release_approval.h"\n')
        for selector in (1,2,3,4,5):
            result = subprocess.run(base+['-DD008_PRODUCT_PROFILE=%d'%selector], capture_output=True, text=True)
            assert result.returncode != 0 and 'Product release is not approved' in result.stderr
    print('PASS all five explicit configurations preprocess; unapproved release is rejected')


if __name__ == "__main__":
    check_d008_20s_profile_contract_check()
    check_d008_profile_selection_host_check()

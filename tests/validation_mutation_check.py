"""故意破坏外部临时副本，确认场景可捕捉关键安全语义变化；从不改工作树。"""
from pathlib import Path
import tempfile
from validation_support import read, run_c, evidence

source = read('bms/core/bms_sw_protection.c')
mutations = {
    'trip-equality': ('(value >= trip)', '(value > trip)'),
    'recovery-equality': ('(value <= recover)', '(value < recover)'),
    'filter-ceiling': ('delay_ms + BMS_SW_PROTECTION_SAMPLE_MS - 1u', 'delay_ms'),
    'mos-charge-gate': ('f->b1CellChgOtp || f->b1CellChgUtp || f->b1TmosOtp ||', 'f->b1CellChgOtp || f->b1CellChgUtp ||'),
}
with tempfile.TemporaryDirectory(prefix='bms-mutants-') as directory:
    for name, (old, new) in mutations.items():
        assert source.count(old) == 1, '变异位置已变化，需人工更新：' + name
        mutant = Path(directory)/(name+'.c')
        mutant.write_text(source.replace(old,new),encoding='utf-8')
        run_c(read('tests/fixtures/protection_scenarios.c'),
              [str(mutant),'bms/core/bms_state.c'],name=name,expect_failure=True)
evidence({'domain':'test_quality','killed_mutations':list(mutations),
          'boundary':'仅这四种保护变异，不代表所有模块的 mutation score'})

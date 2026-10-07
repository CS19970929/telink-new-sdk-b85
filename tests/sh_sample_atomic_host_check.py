"""Run complete production SH sampling with device read/cell fault injection."""
from sh_recovery_host_check import check_sh_recovery_evidence_host_check as run

run(("-DATOMIC_TEST=1",))

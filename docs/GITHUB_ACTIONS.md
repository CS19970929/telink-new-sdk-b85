# GitHub Actions for the TLSR8251 BMS

This repository uses a two-layer CI model so pull requests get fast feedback without pretending that a generic hosted compiler is equivalent to the Telink TC32 production toolchain.

## 1. GitHub-hosted contract checks

`Host contract checks` runs on `ubuntu-latest` for pushes and pull requests. It does not build production firmware. It verifies the repository/tooling contracts that are portable across hosts:

```text
python bms_tools/bms.py sources --check
python -m unittest tests.test_bms_tools -v
python tests/dvc1124_config_quick_check.py
python tests/flash_quick_check.py
```

This job is intended to be a required PR check.

## 2. Production TC32 build

`TC32 production build` runs only when the repository Actions variable below is enabled:

```text
TELINK_TC32_CI_ENABLED=1
```

It requires a Windows self-hosted runner with these labels:

```text
self-hosted
Windows
X64
telink-tc32
```

The runner must provide the same production dependencies expected by `bms_tools/bms.py`:

- Python 3
- Telink TC32 toolchain, including the pinned `tc32-elf-gcc 4.5.1-tc32-1.3` environment used by this project
- GNU Make required by the existing scripted build
- `cppcheck`
- the repository's existing Telink `tl_check_fw2.exe`

The GitHub job deliberately invokes the existing `bms_tools/bms.py` commands instead of maintaining a second build definition. The production sequence is:

```text
env
sources --check
rebuild
check-fw
size
map
manifest
verify
static --no-report
host contract tests
```

`static --no-report` is used in CI so the runner does not depend on the machine-specific Excel report template path. Machine-readable static-analysis evidence is still generated.

## Artifacts

When the TC32 job runs, GitHub Actions uploads the production evidence for 14 days, including where generated:

- `.bin`
- `.elf`
- `.map`
- `.lst`
- firmware integrity manifest
- build log
- static-analysis machine-readable output

## Recommended branch protection

For `refactor/bms-template-phase1`, require `Host contract checks` before merging. After a permanent Windows TC32 runner is available and `TELINK_TC32_CI_ENABLED=1` is enabled, also require `TC32 production build` for changes intended for release.

Do not replace the TC32 production gate with `arm-none-eabi-gcc`, host GCC, or another generic compiler. Those tools do not prove that the TLSR8251 production firmware links and packages correctly.

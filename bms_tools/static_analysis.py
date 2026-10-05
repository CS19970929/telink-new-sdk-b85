"""Cppcheck 的编译覆盖、结果归并及报告流程。

build 提供实际构建的路径与工具调用；本模块不维护第二套产品/编译选项。
"""
import argparse
import csv
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path


class StaticAnalysis:
    def __init__(self, build):
        self.build = build

    def _write_json(self, path: Path, value: object) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


    def _read_cppcheck_config(self) -> list[str]:
        if not self.build.CPPCHECK_CONFIG.exists():
            self.build._die(f"cppcheck config missing: {self.build.CPPCHECK_CONFIG}")
        args: list[str] = []
        for raw_line in self.build.CPPCHECK_CONFIG.read_text(encoding="utf-8").splitlines():
            line = raw_line.strip()
            if not line or line.startswith(";") or line.startswith("#"):
                continue
            args.extend(shlex.split(line, posix=False))
        return args


    def _capture_real_compile_database(self, out_dir: Path) -> tuple[list[dict], list[str], str]:
        """Capture compile commands from the same Make driver used by build/rebuild.

        ``make -B -n`` expands build.mk and the generated per-source rules without
        compiling.  This keeps Cppcheck synchronized with the real source list,
        include paths, defines, language standard and TC32-specific compiler flags.
        """
        entries = self.build._load_source_order_strict()
        self.build._gen_sources_mk()
        env = self.build._ensure_toolchain_env(dict(os.environ))
        make = self.build._need_make()
        env["EXTRA_DEFINES"] = self.build._effective_extra_defines()
        command = [
            make, "-B", "-n", "--no-print-directory", "-f", str(self.build._HERE / "build.mk"),
            f"REPO_ROOT={self.build._junc(self.build.REPO_ROOT).as_posix()}",
            f"SDK_DIR={self.build._junc(self.build.SDK_DIR).as_posix()}",
            f"BUILD_DIR={self.build._junc(self.build.BUILD_DIR).as_posix()}",
            f"PRODUCT={self.build.PRODUCT}",
            f"AFE_BACKEND={'dvc1124' if self.build.PRODUCT == 'd008' else 'sh3673510'}",
            "all",
        ]
        result = subprocess.run(command, cwd=str(self.build._junc(self.build.REPO_ROOT)), env=env, check=False,
                                capture_output=True, text=True)
        dry_run = result.stdout or ""
        (out_dir / "make_dry_run.log").write_text(dry_run, encoding="utf-8")
        if result.returncode != 0:
            self.build._die(f"failed to extract real compile commands (rc={result.returncode}); "
                 f"see {out_dir / 'make_dry_run.log'}")
        compile_lines = [line.strip() for line in dry_run.splitlines()
                         if re.match(r"^(?:.*[/\\])?tc32-elf-gcc(?:\.exe)?\s", line.strip(), re.I)
                         and " -c " in f" {line.strip()} "]
        if len(compile_lines) != len(entries):
            self.build._die("real compile command count does not match source_order.txt: "
                 f"commands={len(compile_lines)} sources={len(entries)}")

        database: list[dict] = []
        for rel, compile_line in zip(entries, compile_lines):
            source = self.build._junc(self.build.REPO_ROOT / rel).as_posix()
            output = self.build._junc(self.build.OBJ_DIR / Path(rel).with_suffix(".o")).as_posix()
            if source.casefold() not in compile_line.replace("\\", "/").casefold():
                self.build._die(f"dry-run command/source order mismatch for {rel}")
            database.append({
                "directory": self.build._junc(self.build.REPO_ROOT).as_posix(),
                "command": compile_line,
                "file": source,
                "output": output,
            })
        self._write_json(out_dir / "compile_commands_build.json", database)
        return database, entries, dry_run


    def _git_changed_sdk_paths(self) -> tuple[set[str] | None, str | None]:
        """Return SDK paths changed from the documented product baseline.

        ``git diff <baseline> --`` includes committed changes and working-tree
        changes.  If provenance cannot be established, callers conservatively scan
        every compiled C translation unit instead of risking a missed modified SDK
        file.
        """
        check = subprocess.run(["git", "cat-file", "-e", f"{self.build.SDK_BASELINE_COMMIT}^{{commit}}"],
                               cwd=str(self.build.REPO_ROOT), capture_output=True, text=True, check=False)
        if check.returncode != 0:
            return None, f"baseline commit unavailable: {self.build.SDK_BASELINE_COMMIT}"
        result = subprocess.run(
            ["git", "diff", "--name-only", self.build.SDK_BASELINE_COMMIT, "--", self.build.SDK_SUBDIR],
            cwd=str(self.build.REPO_ROOT), capture_output=True, text=True, check=False,
        )
        if result.returncode != 0:
            return None, (result.stderr or "git diff failed").strip()
        prefix = self.build.SDK_SUBDIR.rstrip("/") + "/"
        changed = {
            line.strip().replace("\\", "/")[len(prefix):]
            for line in result.stdout.splitlines()
            if line.strip().replace("\\", "/").startswith(prefix)
        }
        return changed, None


    def _canonical_repo_path(self, value: str) -> tuple[str, Path | None]:
        normal = value.replace("\\", "/")
        junction = self.build.JUNCTION.as_posix().rstrip("/") + "/"
        if normal.casefold().startswith(junction.casefold()):
            normal = self.build.REPO_ROOT.as_posix().rstrip("/") + "/" + normal[len(junction):]
        candidate = Path(normal)
        try:
            resolved = candidate.resolve()
            return resolved.relative_to(self.build.REPO_ROOT).as_posix(), resolved
        except (OSError, ValueError):
            return normal, candidate if candidate.exists() else None


    def _sdk_relative_path(self, value: str) -> str | None:
        """Return a repository-relative path for repo, junction or absolute inputs."""
        normal = value.replace("\\", "/")
        junction_prefix = self.build.JUNCTION.as_posix().rstrip("/") + "/"
        if normal.casefold().startswith(junction_prefix.casefold()): return normal[len(junction_prefix):]
        try:
            candidate = Path(normal)
            if not candidate.is_absolute(): candidate = self.build.REPO_ROOT / candidate
            return candidate.resolve().relative_to(self.build.REPO_ROOT).as_posix()
        except (OSError, ValueError):
            return None


    def _is_application_scope_path(self, value: str) -> bool:
        sdk_relative = self._sdk_relative_path(value)
        return sdk_relative is not None and sdk_relative.startswith(self.build.PROJECT_SOURCE_PREFIX)


    def _cppcheck_scope_path(self, value: str) -> str:
        candidate = Path(value)
        if not candidate.is_absolute():
            candidate = self.build.REPO_ROOT / candidate
        return self.build._junc(candidate).as_posix()


    def _write_cppcheck_scope_exclusions(self, dependencies: set[str], out_dir: Path) -> tuple[Path, list[str]]:
        """Exclude dependency diagnostics outside the user-owned application.

        Cppcheck must still parse SDK headers so application types, macros and
        conditional compilation remain faithful to the real TC32 build.  This
        generated list changes only the diagnostic boundary; no application
        finding is suppressed.
        """
        excluded = sorted(path for path in dependencies if not self._is_application_scope_path(path))
        suppression_path = out_dir / "cppcheck-sdk-scope-exclusions.txt"
        suppression_path.write_text(
            "\n".join(f"*:{self._cppcheck_scope_path(path)}" for path in excluded) + "\n",
            encoding="utf-8",
        )
        evidence = []
        for value in excluded:
            path = Path(value)
            if not path.is_absolute():
                path = self.build.REPO_ROOT / path
            evidence.append({
                "file": value,
                "kind": "头文件" if path.suffix.lower() == ".h" else "依赖文件",
                "reason": "不属于 bms/ 应用层检查范围；仅为真实编译依赖解析",
                "sha256": self.build._sha256(path) if path.exists() and path.is_file() else "",
            })
        self._write_json(out_dir / "sdk_scope_exclusions.json", {
            "policy": self.build.STATIC_SCOPE_POLICY,
            "application_prefix": self.build.PROJECT_SOURCE_PREFIX,
            "excluded_dependency_count": len(excluded),
            "excluded_dependencies": evidence,
            "note": "范围排除不阻止 Cppcheck 解析 SDK 头文件；应用层诊断未使用 suppression。",
        })
        return suppression_path, excluded


    def _assert_application_diagnostics(self, rows: list[dict], evidence_path: Path) -> None:
        outside = []
        for row in rows:
            locations = row.get("locations", [])
            if locations and not self._is_application_scope_path(locations[0].get("file", "")):
                outside.append(f"{row.get('id')}:{locations[0].get('file')}:{locations[0].get('line')}")
        if outside:
            self.build._die("Cppcheck emitted diagnostics outside bms/ scope: "
                 + ", ".join(outside[:5]) + f"; see {evidence_path}")


    def _dependency_runtime_token(self, value: str) -> str:
        """Map Make-only junction paths back to the current checkout.

        GNU Make needs the fixed, space-free junction, but dependency probing is
        invoked through subprocess argv and can safely use the real checkout path.
        Avoiding the shared junction here also prevents a persistent/concurrent
        Windows runner from auditing a stale checkout.
        """
        normal = value.replace("\\", "/")
        junction = self.build.JUNCTION.as_posix().rstrip("/")
        index = normal.casefold().find(junction.casefold())
        if index < 0:
            return value
        return normal[:index] + self.build.REPO_ROOT.as_posix().rstrip("/") + normal[index + len(junction):]


    def _analyse_dependency_graph(self, analysis_database: list[dict], out_dir: Path) -> set[str]:
        """Use the real TC32 commands in dependency-only mode to audit headers."""
        dependencies: set[str] = set()
        logs: list[str] = []
        env = self.build._ensure_toolchain_env(dict(os.environ))
        for entry in analysis_database:
            tokens = shlex.split(entry["command"], posix=True)
            filtered: list[str] = [str(self.build._tc32_tool("tc32-elf-gcc"))]
            skip_next = False
            for token in tokens[1:]:
                if skip_next:
                    skip_next = False
                    continue
                if token == "-c":
                    continue
                if token == "-o":
                    skip_next = True
                    continue
                if token.startswith("-o"):
                    continue
                if token.lower().endswith((".c", ".s")):
                    continue
                filtered.append(self._dependency_runtime_token(token))
            runtime_file = self._dependency_runtime_token(entry["file"])
            runtime_dir = self._dependency_runtime_token(entry["directory"])
            filtered.extend(["-MM", runtime_file])
            result = subprocess.run(filtered, cwd=runtime_dir, env=env,
                                    capture_output=True, text=True, check=False)
            logs.append(f"# {entry['file']} rc={result.returncode}\n"
                        f"cwd={runtime_dir}\n"
                        f"command={subprocess.list2cmdline(filtered)}\n"
                        f"{result.stdout}{result.stderr}")
            if result.returncode != 0:
                (out_dir / "dependencies.log").write_text("\n".join(logs), encoding="utf-8")
                self.build._die(f"TC32 dependency audit failed for {entry['file']}; "
                     f"see {out_dir / 'dependencies.log'}")
            flattened = re.sub(r"\\\s*\r?\n", " ", result.stdout or "")
            payload = flattened.split(":", 1)[1] if ":" in flattened else ""
            # GCC emits Makefile syntax where spaces are escaped as ``\ ``.  A
            # plain split truncates every dependency at the first escaped space
            # and can accidentally suppress a parent directory instead of the
            # exact SDK header.  shlex removes the Make escape while preserving
            # the complete path as one token.
            for token in shlex.split(payload, posix=True):
                path = token.strip()
                rel, resolved = self._canonical_repo_path(path)
                if resolved is not None and resolved.exists():
                    dependencies.add(rel)
        (out_dir / "dependencies.log").write_text("\n".join(logs), encoding="utf-8")
        return dependencies


    def _compiler_predefines(self, dependency_paths: set[str], out_dir: Path) -> tuple[dict[str, str], dict[str, str]]:
        compiler = self.build._tc32_tool("tc32-elf-gcc")
        result = subprocess.run([compiler, "-dM", "-E", "-x", "c", "-"], input="",
                                capture_output=True, text=True, check=False)
        if result.returncode != 0:
            self.build._die("unable to read TC32 compiler predefined macros")
        (out_dir / "compiler_predefines.txt").write_text(result.stdout, encoding="utf-8")
        all_defs: dict[str, str] = {}
        for line in result.stdout.splitlines():
            match = re.match(r"#define\s+([A-Za-z_]\w*)(?:\s+(.*))?$", line)
            if match:
                all_defs[match.group(1)] = (match.group(2) or "1").strip()

        conditional_names: set[str] = set()
        for rel in dependency_paths:
            path = self.build.REPO_ROOT / rel
            if path.suffix.lower() not in (".c", ".h") or not path.exists():
                continue
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                match = re.match(r"\s*#\s*(?:if|elif|ifdef|ifndef)\b(.*)$", line)
                if match:
                    conditional_names.update(re.findall(r"\b[A-Za-z_]\w*\b", match.group(1)))
        applied = {
            name: value for name, value in all_defs.items()
            if name in conditional_names and len(value) <= 80 and "\"" not in value
        }
        self._write_json(out_dir / "compiler_predefines_applied.json", applied)
        return all_defs, applied


    def _extract_real_compile_settings(self, command: str) -> dict:
        tokens = shlex.split(command, posix=True)
        includes = [token[2:] for token in tokens if token.startswith("-I")]
        defines = [token[2:] for token in tokens if token.startswith("-D")]
        standard = next((token.split("=", 1)[1] for token in tokens
                         if token.startswith("-std=")), None)
        compile_flags = [token for token in tokens[1:]
                         if not token.startswith(("-I", "-D", "-o"))
                         and token != "-c" and not token.lower().endswith((".c", ".s"))]
        return {
            "compiler": str(self.build._tc32_tool("tc32-elf-gcc")),
            "compiler_version": self.build._tool_version([str(self.build._tc32_tool("tc32-elf-gcc")), "--version"], 0),
            "include_paths": includes,
            "defines": defines,
            "c_standard": standard,
            "compile_flags": compile_flags,
        }


    def _parse_cppcheck_xml(self, path: Path, engine: str) -> tuple[str, list[dict]]:
        try:
            root = ET.parse(path).getroot()
        except (ET.ParseError, OSError) as exc:
            self.build._die(f"invalid Cppcheck XML {path}: {exc}")
        version_node = root.find("cppcheck")
        version = version_node.get("version", "unknown") if version_node is not None else "unknown"
        rows: list[dict] = []
        for error in root.findall(".//error"):
            locations = []
            for location in error.findall("location"):
                rel, _ = self._canonical_repo_path(location.get("file", ""))
                locations.append({
                    "file": rel,
                    "line": int(location.get("line", "0") or 0),
                    "column": int(location.get("column", "0") or 0),
                    "info": location.get("info", ""),
                })
            analysis_unit, _ = self._canonical_repo_path(error.get("file0", ""))
            eid = error.get("id", "unknown")
            misra_match = re.search(r"misra(?:-c)?(?:2012)?[-:]?(\d+\.\d+)", eid, re.I)
            rows.append({
                "engine": engine,
                "id": eid,
                "severity": error.get("severity", "unknown"),
                "message": error.get("msg", ""),
                "verbose": error.get("verbose", ""),
                "cwe": error.get("cwe", ""),
                "inconclusive": error.get("inconclusive", "false").lower() == "true",
                "symbol": (error.findtext("symbol") or ""),
                "analysis_unit": analysis_unit,
                "misra_rule": misra_match.group(1) if misra_match else "",
                "locations": locations,
            })
        return version, rows


    def _function_at_line(self, path: Path | None, target_line: int) -> str:
        """Best-effort C function locator; global/header diagnostics remain explicit."""
        if path is None or not path.exists() or target_line <= 0:
            return "<全局/未定位>"
        text = path.read_text(encoding="utf-8", errors="replace")
        cleaned: list[str] = []
        in_block = False
        for raw in text.splitlines():
            line: list[str] = []
            i = 0
            in_string: str | None = None
            while i < len(raw):
                pair = raw[i:i + 2]
                if in_block:
                    if pair == "*/":
                        in_block = False
                        i += 2
                    else:
                        line.append(" ")
                        i += 1
                    continue
                if in_string:
                    if raw[i] == "\\":
                        line.extend("  ")
                        i += 2
                    elif raw[i] == in_string:
                        line.append(" ")
                        in_string = None
                        i += 1
                    else:
                        line.append(" ")
                        i += 1
                    continue
                if pair == "/*":
                    in_block = True
                    line.extend("  ")
                    i += 2
                elif pair == "//":
                    line.extend(" " * (len(raw) - i))
                    break
                elif raw[i] in ('"', "'"):
                    in_string = raw[i]
                    line.append(" ")
                    i += 1
                else:
                    line.append(raw[i])
                    i += 1
            cleaned.append("".join(line))

        depth = 0
        current = "<全局/未定位>"
        function_depth: int | None = None
        signature = ""
        controls = {"if", "for", "while", "switch", "sizeof", "return"}
        for number, line in enumerate(cleaned, 1):
            signature = (signature + " " + line.strip())[-1200:]
            if "{" in line:
                before = signature[:signature.rfind("{")]
                candidates = re.findall(r"([A-Za-z_]\w*)\s*\([^;{}]*\)\s*$", before)
                if candidates and candidates[-1] not in controls and not re.search(r"\btypedef\b", before):
                    current = candidates[-1]
                    function_depth = depth + 1
                signature = signature[signature.rfind("{") + 1:]
            depth += line.count("{") - line.count("}")
            if function_depth is not None and depth < function_depth:
                current = "<全局/未定位>"
                function_depth = None
            if number >= target_line:
                return current
            if ";" in line and depth == 0:
                signature = signature[signature.rfind(";") + 1:]
        return current


    def _finding_classification(self, row: dict, modified_sdk: set[str]) -> tuple[str, str, str]:
        location = row["locations"][0] if row["locations"] else {"file": "", "line": 0}
        file = location["file"]
        sdk_rel = file[len(self.build.SDK_SUBDIR.rstrip("/") + "/"):] if file.startswith(self.build.SDK_SUBDIR.rstrip("/") + "/") else file
        config_ids = {"missingInclude", "missingIncludeSystem", "syntaxError", "preprocessorErrorDirective",
                      "toomanyconfigs", "internalAstError", "internalError", "unhandledCharLiteral"}
        if row["misra_rule"]:
            return "MISRA违规（待评审）", "待评审", "按规则逐项评审；需要保留时走正式 Deviation 审批"
        if row["id"] in config_ids or not file:
            return "配置导致的问题", "待修正配置", "先修正分析配置并重跑，不能作为代码问题关闭"
        is_project = sdk_rel.startswith(self.build.PROJECT_SOURCE_PREFIX) or sdk_rel in modified_sdk
        if not is_project:
            deviation_ids = {"badBitmaskCheck", "clarifyCalculation", "invalidPointerCast",
                             "reinterpretCast", "integerOverflow"}
            if row["id"] in deviation_ids or row["severity"] in ("error", "warning", "portability"):
                return "SDK问题", "Deviation候选（未批准）", "不要直接修改官方 SDK；评估影响并提交人工 Deviation 审批"
            return "SDK问题", "待评审（SDK）", "核实是否为头文件/内联函数上下文告警；不要直接修改官方 SDK"
        if row["severity"] in ("error", "warning"):
            return "真实代码问题（待确认）", "待整改", "建议优先整改并执行固件与硬件回归"
        return "代码质量建议", "待评审", "评审可维护性与风险；不修改时记录技术依据"


    def _deduplicate_findings(self, raw_rows: list[dict], modified_sdk: set[str]) -> list[dict]:
        merged: dict[tuple, dict] = {}
        for row in raw_rows:
            location = row["locations"][0] if row["locations"] else {"file": "", "line": 0, "column": 0}
            key = (row["engine"], row["id"], location["file"], location["line"], row["message"], row["misra_rule"])
            if key not in merged:
                resolved = self.build.REPO_ROOT / location["file"] if location["file"] else None
                classification, status, recommendation = self._finding_classification(row, modified_sdk)
                merged[key] = {
                    "engine": row["engine"],
                    "file": location["file"],
                    "function": self._function_at_line(resolved, location["line"]),
                    "line": location["line"],
                    "column": location.get("column", 0),
                    "id": row["id"],
                    "severity": row["severity"],
                    "message": row["message"],
                    "verbose": row["verbose"],
                    "cwe": row["cwe"],
                    "misra_rule": row["misra_rule"],
                    "inconclusive": row["inconclusive"],
                    "classification": classification,
                    "status": status,
                    "recommendation": recommendation,
                    "analysis_units": set(),
                    "occurrence_count": 0,
                }
            merged[key]["occurrence_count"] += 1
            if row["analysis_unit"]:
                merged[key]["analysis_units"].add(row["analysis_unit"])
        findings = []
        for value in merged.values():
            value["analysis_units"] = sorted(value["analysis_units"])
            findings.append(value)
        order = {"error": 0, "warning": 1, "performance": 2, "portability": 3,
                 "style": 4, "information": 5, "debug": 6}
        findings.sort(key=lambda item: (order.get(item["severity"], 99), item["file"], item["line"], item["id"]))
        return findings


    def _write_findings_csv(self, path: Path, findings: list[dict]) -> None:
        fields = ["file", "function", "line", "column", "id", "severity", "message", "cwe",
                  "misra_rule", "classification", "status", "recommendation", "inconclusive",
                  "occurrence_count", "analysis_units"]
        with path.open("w", encoding="utf-8-sig", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fields)
            writer.writeheader()
            for item in findings:
                row = {key: item.get(key, "") for key in fields}
                row["analysis_units"] = "; ".join(item["analysis_units"])
                writer.writerow(row)


    def _find_misra_addon(self) -> Path | None:
        candidates = [
            self.build.DEFAULT_CPPCHECK.parent / "addons" / "misra.py",
            self.build.DEFAULT_CPPCHECK.parent / "misra.py",
        ]
        return next((path for path in candidates if path.exists()), None)


    def _run_cppcheck(self, cmd: list[str], xml_path: Path, log_path: Path) -> int:
        full = [*cmd, f"--output-file={self.build._junc(xml_path)}", "--xml-version=2"]
        result = subprocess.run(full, cwd=str(self.build.REPO_ROOT), capture_output=True, text=True, check=False)
        log_path.write_text(
            "COMMAND:\n" + subprocess.list2cmdline(full) + "\n\nSTDOUT:\n" + (result.stdout or "")
            + "\nSTDERR:\n" + (result.stderr or ""), encoding="utf-8",
        )
        if result.returncode != 0:
            self.build._die(f"Cppcheck failed rc={result.returncode}; see {log_path}")
        return result.returncode


    def _resolve_artifact_runtime(self) -> Path:
        python_candidates: list[Path] = []
        if os.environ.get("BMS_ARTIFACT_PYTHON"):
            python_candidates.append(Path(os.environ["BMS_ARTIFACT_PYTHON"]))
        profile = os.environ.get("USERPROFILE")
        if profile:
            bundled = (Path(profile) / ".cache" / "codex-runtimes" / "codex-primary-runtime"
                       / "dependencies" / "python" / "python.exe")
            python_candidates.append(bundled)
        python_candidates.append(Path(sys.executable))
        probe = "import artifact_tool_v2"
        for python in python_candidates:
            if not python.exists():
                continue
            result = subprocess.run([str(python), "-c", probe], capture_output=True,
                                    text=True, check=False)
            if result.returncode == 0:
                return python.resolve()
        self.build._die("Excel report runtime unavailable. Set BMS_ARTIFACT_PYTHON to a Python "
             "runtime containing artifact_tool_v2.")
        raise AssertionError("unreachable")


    def _run_static_report_builder(self, template: Path, data_path: Path, output_path: Path,
                                   preview_dir: Path) -> None:
        if not template.exists():
            self.build._die(f"static analysis report template missing: {template}")
        if not self.build.STATIC_REPORT_BUILDER.exists():
            self.build._die(f"static report builder missing: {self.build.STATIC_REPORT_BUILDER}")
        python = self._resolve_artifact_runtime()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        # The desktop artifact runtime owns a lifecycle for its interactive output
        # directory. Keep that lifecycle isolated from Cppcheck evidence, then copy
        # only completed files into the immutable run directory.
        with tempfile.TemporaryDirectory(prefix="bms_static_report_") as temp_name:
            temp_root = Path(temp_name)
            temp_data = temp_root / data_path.name
            temp_output = temp_root / output_path.name
            temp_preview = temp_root / "report_verification"
            shutil.copy2(data_path, temp_data)
            result = subprocess.run(
                [str(python), str(self.build.STATIC_REPORT_BUILDER), str(template), str(temp_data),
                 str(temp_output), str(temp_preview)],
                cwd=str(temp_root), capture_output=True, text=True,
                encoding="utf-8", errors="replace", check=False,
            )
            if result.returncode == 0 and temp_output.exists():
                shutil.copy2(temp_output, output_path)
                verification = temp_preview / "verification.json"
                if verification.exists():
                    preview_dir.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(verification, preview_dir / verification.name)
        (data_path.parent / "report_builder.log").write_text(
            (result.stdout or "") + "\n" + (result.stderr or ""), encoding="utf-8")
        if result.returncode != 0 or not output_path.exists():
            self.build._die(f"Excel report generation failed; see {data_path.parent / 'report_builder.log'}")


    def cmd_static(self, args: argparse.Namespace) -> int:
        if not self.build.DEFAULT_CPPCHECK.exists():
            self.build._die(f"cppcheck not found: {self.build.DEFAULT_CPPCHECK}")
        if not self.build.CPPCHECK_PLATFORM.exists():
            self.build._die(f"TC32 Cppcheck platform missing: {self.build.CPPCHECK_PLATFORM}")

        started_at = self.build._now_iso()
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        static_root = self.build.BUILD_DIR / "static"
        run_dir = static_root / f"run_{stamp}"
        run_dir.mkdir(parents=True, exist_ok=False)

        full_database, source_order, _ = self._capture_real_compile_database(run_dir)
        changed_sdk, baseline_error = self._git_changed_sdk_paths()
        compiled_c = {rel for rel in source_order if rel.endswith(".c")}
        modified_sdk: set[str] = set() if changed_sdk is None else {
            path for path in changed_sdk if not path.startswith(self.build.PROJECT_SOURCE_PREFIX)
        }
        selected_sources = {
            rel for rel in compiled_c if rel.startswith(self.build.PROJECT_SOURCE_PREFIX)
        }
        selection_note = self.build.STATIC_SCOPE_POLICY

        rel_to_entry = dict(zip(source_order, full_database))
        analysis_database = [rel_to_entry[rel] for rel in source_order if rel in selected_sources]
        self._write_json(run_dir / "compile_commands_analysis.json", analysis_database)
        dependencies = self._analyse_dependency_graph(analysis_database, run_dir)
        dependencies.update(
            (self.build.REPO_ROOT / rel).relative_to(self.build.REPO_ROOT).as_posix() for rel in selected_sources
        )
        scope_suppression_path, excluded_dependencies = self._write_cppcheck_scope_exclusions(
            dependencies, run_dir)
        _, applied_predefs = self._compiler_predefines(dependencies, run_dir)

        cppcheck_args = self._read_cppcheck_config()
        project_arg = f"--project={self.build._junc(run_dir / 'compile_commands_analysis.json')}"
        platform_arg = f"--platform={self.build._junc(self.build.CPPCHECK_PLATFORM)}"
        predef_args = [f"-D{name}={value}" for name, value in sorted(applied_predefs.items())]
        scope_args = [
            f"--suppressions-list={self.build._junc(scope_suppression_path)}",
            "--suppress=unmatchedSuppression",
        ]
        config_cmd = [str(self.build.DEFAULT_CPPCHECK), "--check-config", project_arg, platform_arg,
                      *scope_args, *predef_args]
        config_check = subprocess.run(config_cmd, cwd=str(self.build.REPO_ROOT), capture_output=True,
                                      text=True, check=False)
        (run_dir / "configuration_check.log").write_text(
            "COMMAND:\n" + subprocess.list2cmdline(config_cmd) + "\n\nSTDOUT:\n"
            + (config_check.stdout or "") + "\nSTDERR:\n" + (config_check.stderr or ""),
            encoding="utf-8",
        )
        if config_check.returncode != 0:
            self.build._die(f"Cppcheck configuration audit failed; see {run_dir / 'configuration_check.log'}")

        native_xml = run_dir / "cppcheck-native.xml"
        native_cache = run_dir / "cppcheck-cache-native"
        native_cache.mkdir(parents=True, exist_ok=True)
        native_cmd = [
            str(self.build.DEFAULT_CPPCHECK), *cppcheck_args, project_arg, platform_arg,
            f"--cppcheck-build-dir={self.build._junc(native_cache)}",
            f"--checkers-report={self.build._junc(run_dir / 'cppcheck-checkers-report.txt')}",
            "-j", str(max(1, args.jobs)), *scope_args, *predef_args,
        ]
        self.build._info(f"cppcheck native: {len(analysis_database)} C translation units from real compile database")
        self._run_cppcheck(native_cmd, native_xml, run_dir / "cppcheck-native.log")
        cppcheck_version, raw_native_all = self._parse_cppcheck_xml(native_xml, "Cppcheck原生")
        tool_metadata = [row for row in raw_native_all if row["id"] in {"checkersReport"}]
        raw_native = [row for row in raw_native_all if row not in tool_metadata]
        self._assert_application_diagnostics(raw_native, run_dir / "sdk_scope_exclusions.json")

        addon = self._find_misra_addon()
        raw_misra: list[dict] = []
        misra_status: dict = {
            "executed": False,
            "addon": str(addon) if addon else None,
            "coverage": "未执行：本机 Cppcheck 安装不包含 MISRA addon；原生告警不计为 MISRA 违规",
        }
        if addon is not None:
            misra_xml = run_dir / "cppcheck-misra.xml"
            misra_cache = run_dir / "cppcheck-cache-misra"
            misra_cache.mkdir(parents=True, exist_ok=True)
            misra_cmd = [
                str(self.build.DEFAULT_CPPCHECK), "--enable=warning", "--std=c99", "--language=c",
                project_arg, platform_arg, f"--addon={addon}",
                f"--cppcheck-build-dir={self.build._junc(misra_cache)}",
                "-j", str(max(1, args.jobs)), *scope_args, *predef_args,
            ]
            self._run_cppcheck(misra_cmd, misra_xml, run_dir / "cppcheck-misra.log")
            _, addon_rows = self._parse_cppcheck_xml(misra_xml, "Cppcheck MISRA addon")
            raw_misra = [row for row in addon_rows if row["misra_rule"]]
            self._assert_application_diagnostics(raw_misra, run_dir / "sdk_scope_exclusions.json")
            misra_status = {
                "executed": True,
                "addon": str(addon),
                "coverage": "Cppcheck MISRA addon 仅覆盖其实现的可自动检查规则；不代表完整 MISRA C 合规",
            }
        self._write_json(run_dir / "misra_capability.json", misra_status)

        findings = self._deduplicate_findings(raw_native + raw_misra, set())
        self._write_json(run_dir / "findings.json", findings)
        self._write_findings_csv(run_dir / "findings.csv", findings)

        header_prefixes = ("bms/core/", "bms/app/", "bms/platform/telink/",
                           f"bms/products/{self.build.PRODUCT}/", f"bms/afe/{'dvc1124' if self.build.PRODUCT == 'd008' else 'sh3673510'}/")
        project_headers = sorted(
            path.relative_to(self.build.REPO_ROOT).as_posix()
            for path in (self.build.REPO_ROOT / "bms").rglob("*.h")
            if path.relative_to(self.build.REPO_ROOT).as_posix().startswith(header_prefixes)
        )
        dependency_sdk_rel = {
            rel[len(self.build.SDK_SUBDIR.rstrip("/") + "/"):]
            for rel in dependencies if rel.startswith(self.build.SDK_SUBDIR.rstrip("/") + "/")
        }
        dependency_headers = {rel for rel in dependencies if rel.endswith(".h")}
        project_dependency_headers = {
            rel for rel in dependency_headers if rel.startswith(self.build.PROJECT_SOURCE_PREFIX)
        }
        sdk_dependency_headers = dependency_headers - project_dependency_headers
        scope_rows = []
        for rel in source_order:
            analysed = rel in selected_sources
            reason = ("直接作为 C 翻译单元分析" if analysed else
                      "汇编源文件，Cppcheck 不支持" if rel.endswith(".S") else
                      "官方 SDK 不属于用户确认的静态检查范围")
            scope_rows.append({
                "file": rel,
                "kind": "C翻译单元" if rel.endswith(".c") else "汇编源文件",
                "owner": "BMS应用层" if rel.startswith(self.build.PROJECT_SOURCE_PREFIX) else "Telink官方SDK（范围排除）",
                "modified_from_baseline": changed_sdk is not None and rel in changed_sdk,
                "participates_in_build": True,
                "cppcheck_mode": "直接分析" if analysed else "不检查",
                "exclusion_reason": "" if analysed else reason,
                "sha256": self.build._sha256(self.build.REPO_ROOT / rel),
            })
        for rel in project_headers:
            covered = rel in dependencies
            scope_rows.append({
                "file": rel,
                "kind": "头文件",
                "owner": "BMS应用层",
                "modified_from_baseline": changed_sdk is not None and rel in changed_sdk,
                "participates_in_build": covered,
                "cppcheck_mode": "随翻译单元解析" if covered else "未被真实构建依赖引用",
                "exclusion_reason": "" if covered else "真实编译配置未引用该头文件",
                "sha256": self.build._sha256(self.build.REPO_ROOT / rel),
            })
        for rel in sorted(sdk_dependency_headers):
            scope_rows.append({
                "file": rel,
                "kind": "SDK依赖头文件",
                "owner": "Telink官方SDK（范围排除）",
                "modified_from_baseline": changed_sdk is not None and rel in changed_sdk,
                "participates_in_build": True,
                "cppcheck_mode": "仅解析（诊断排除）",
                "exclusion_reason": "应用层真实编译依赖；按用户确认的范围策略不检查SDK问题",
                "sha256": self.build._sha256(self.build.REPO_ROOT / rel),
            })
        self._write_json(run_dir / "scope_audit.json", scope_rows)
        with (run_dir / "scope_audit.csv").open("w", encoding="utf-8-sig", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(scope_rows[0]))
            writer.writeheader()
            writer.writerows(scope_rows)

        severity_counts: dict[str, int] = {}
        classification_counts: dict[str, int] = {}
        id_counts: dict[str, int] = {}
        for finding in findings:
            severity_counts[finding["severity"]] = severity_counts.get(finding["severity"], 0) + 1
            classification_counts[finding["classification"]] = classification_counts.get(finding["classification"], 0) + 1
            id_counts[finding["id"]] = id_counts.get(finding["id"], 0) + 1
        deviation_candidates = [finding for finding in findings if finding["status"].startswith("Deviation候选")]
        uncovered_project_headers = [rel for rel in project_headers if rel not in dependencies]
        coverage_gaps = uncovered_project_headers
        compile_settings = self._extract_real_compile_settings(full_database[0]["command"])
        compile_settings.update({
            "target_mcu": self.build.DECLARED_MCU,
            "startup_profile": self.build.STARTUP_PROFILE,
            "target_configuration_risk": self.build.TARGET_CONFIGURATION_RISK,
            "platform_model": str(self.build.CPPCHECK_PLATFORM.relative_to(self.build.REPO_ROOT)).replace("\\", "/"),
            "compiler_predefines_applied": applied_predefs,
            "configuration_source": f"build.mk 经 make -B -n 展开 + bms/products/{self.build.PRODUCT}/sources.txt",
        })
        summary = {
            "format": "tlsr8251-bms-static-analysis/v2",
            "run_id": stamp,
            "started_at": started_at,
            "finished_at": self.build._now_iso(),
            "git": self.build._git_provenance(),
            "sdk_baseline_commit": self.build.SDK_BASELINE_COMMIT,
            "baseline_audit_error": baseline_error,
            "selection_policy": selection_note,
            "scope_policy": self.build.STATIC_SCOPE_POLICY,
            "cppcheck_version": cppcheck_version,
            "cppcheck_config": str(self.build.CPPCHECK_CONFIG.relative_to(self.build.REPO_ROOT)).replace("\\", "/"),
            "actual_build_source_count": len(source_order),
            "actual_build_c_count": len(compiled_c),
            "actual_build_assembly_count": len(source_order) - len(compiled_c),
            "analysis_translation_unit_count": len(analysis_database),
            "analysis_header_dependency_count": len(dependency_headers),
            "application_header_dependency_count": len(project_dependency_headers),
            "sdk_parse_only_header_count": len(sdk_dependency_headers),
            "sdk_diagnostic_exclusion_count": len(excluded_dependencies),
            "project_c_translation_unit_count": sum(rel.startswith(self.build.PROJECT_SOURCE_PREFIX) for rel in compiled_c),
            "modified_sdk_translation_unit_count": 0,
            "modified_sdk_excluded_translation_unit_count": sum(
                rel in modified_sdk for rel in compiled_c if not rel.startswith(self.build.PROJECT_SOURCE_PREFIX)
            ),
            "excluded_official_sdk_c_count": sum(rel.endswith(".c") and rel not in selected_sources for rel in source_order),
            "excluded_assembly_count": sum(rel.endswith(".S") for rel in source_order),
            "coverage_gap_count": len(coverage_gaps),
            "coverage_gaps": coverage_gaps,
            "raw_native_occurrence_count": len(raw_native),
            "tool_metadata": tool_metadata,
            "distinct_finding_count": len(findings),
            "native_distinct_finding_count": sum(not item["misra_rule"] for item in findings),
            "misra_distinct_finding_count": sum(bool(item["misra_rule"]) for item in findings),
            "sdk_distinct_finding_count": 0,
            "misra": misra_status,
            "severity_counts": severity_counts,
            "classification_counts": classification_counts,
            "id_counts": dict(sorted(id_counts.items(), key=lambda item: (-item[1], item[0]))),
            "deviation_candidate_count": len(deviation_candidates),
            "approved_deviation_count": 0,
            "configuration": compile_settings,
            "scope": scope_rows,
            "findings": findings,
            "deviation_candidates": deviation_candidates,
            "report": None,
        }
        data_path = run_dir / "static_analysis_report_data.json"
        self._write_json(data_path, summary)

        report_path: Path | None = None
        if not args.no_report:
            template = Path(args.report_template).resolve() if args.report_template else self.build.DEFAULT_STATIC_REPORT_TEMPLATE
            report_path = run_dir / "XXX-BMS_软件静态分析报告_已填写.xlsx"
            self._run_static_report_builder(template, data_path, report_path, run_dir / "report_previews")
            summary["report"] = report_path.as_posix()
            self._write_json(data_path, summary)

        latest = {
            "run_id": stamp,
            "run_dir": run_dir.as_posix(),
            "summary": data_path.as_posix(),
            "report": report_path.as_posix() if report_path else None,
        }
        self._write_json(static_root / "latest.json", latest)
        self.build._info(f"actual build sources: {len(source_order)} ({len(compiled_c)} C + "
              f"{len(source_order) - len(compiled_c)} assembly)")
        self.build._info(f"cppcheck analysed translation units: {len(analysis_database)}; "
              f"application headers: {len(project_dependency_headers)}; "
              f"SDK headers parse-only: {len(sdk_dependency_headers)}; coverage gaps: {len(coverage_gaps)}")
        self.build._info(f"SDK diagnostics excluded by scope policy: {len(excluded_dependencies)} dependency files")
        self.build._info(f"total issues: {len(findings)} distinct ({len(raw_native)} raw native occurrences); "
              f"severity={severity_counts}")
        self.build._info(f"MISRA: {'executed' if misra_status['executed'] else 'not executed'}; "
              f"findings={summary['misra_distinct_finding_count']}")
        self.build._info(f"static analysis complete -> {run_dir}")
        if report_path:
            self.build._info(f"filled Excel report -> {report_path}")
        return 1 if args.strict and findings else 0

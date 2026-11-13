"""Build the actual WPF clients externally and run lifecycle integration against their assemblies."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

BASE = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--variant", choices=["Windows", "Factory", "both"], default="both")
    args = parser.parse_args()
    out = args.out.resolve()
    if out.is_relative_to(BASE.parent.resolve()):
        raise ValueError("Test output must be outside the repository")
    out.mkdir(parents=True, exist_ok=True)
    artifacts = out / "artifacts"
    records = []
    command = ["pwsh", "-NoProfile", "-File", str(BASE / "test-client-lifecycle.ps1"),
               "-OutputDirectory", str(out / "tests")]
    for variant, project, option in [
        ("Windows", "BmsTool.Windows", "-CustomerAssembly"),
        ("Factory", "BmsFactoryTest.Windows", "-InternalAssembly"),
    ]:
        if args.variant not in (variant, "both"):
            continue
        result = subprocess.run(["dotnet", "build", str(BASE / project / (project + ".csproj")),
                                 "-c", "Release", "--artifacts-path", str(artifacts),
                                 "-p:NuGetAudit=false", "--nologo", "-v:quiet"],
                                capture_output=True, text=True, encoding="utf-8", errors="replace")
        (out / (variant + "-build.log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            print(result.stdout + result.stderr)
            raise SystemExit(result.returncode)
        assembly = artifacts / "bin" / project / "release" / (project + ".dll")
        records.append(dict(variant=variant, assembly=str(assembly),
                            sha256=hashlib.sha256(assembly.read_bytes()).hexdigest()))
        command += [option, str(assembly)]
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    (out / "result.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    (out / "results.json").write_text(json.dumps(
        dict(exit_code=result.returncode, assemblies=records), ensure_ascii=False, indent=2), encoding="utf-8")
    print(result.stdout + result.stderr)
    raise SystemExit(result.returncode)

if __name__ == "__main__":
    main()

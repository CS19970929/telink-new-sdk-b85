"""Compile host C in an external directory with GCC/Clang or native MSVC."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess


def compiler_environment():
    if os.environ.get('CC'):
        # CC may be a compiler path containing spaces; flags belong in HOST_CFLAGS.
        cc = os.environ['CC']
        return ([cc] if Path(cc).is_file() else shlex.split(cc)), dict(os.environ)
    for name in ('cc', 'gcc', 'clang', 'cl'):
        if shutil.which(name):
            return [shutil.which(name)], dict(os.environ)
    if os.name == 'nt':
        vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        if vswhere.is_file():
            root = subprocess.check_output([str(vswhere), '-latest', '-products', '*',
                    '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                    '-property', 'installationPath'], text=True, timeout=30).strip()
            script = Path(root) / 'VC/Auxiliary/Build/vcvars64.bat'
            if script.is_file():
                # Read-only environment initialization, no file operations through cmd.
                result = subprocess.run(['cmd.exe', '/d', '/s', '/c',
                    '""' + str(script) + '" >nul && set"'], capture_output=True,
                    text=True, timeout=30, check=True)
                env = dict(os.environ)
                for line in result.stdout.splitlines():
                    if '=' in line and not line.startswith('='):
                        key, value = line.split('=', 1)
                        if key.lower() == 'path':
                            for old in list(env):
                                if old.lower() == 'path':
                                    del env[old]
                            key = 'PATH'
                        env[key] = value
                cc = shutil.which('cl', path=env.get('PATH'))
                if cc:
                    return [cc], env
    raise RuntimeError('Host C compiler missing: install GCC/Clang or Visual Studio C++ tools; or set CC.')


def compile_c(sources, output, includes=()):
    command, env = compiler_environment()
    msvc = Path(command[0]).stem.lower() in ('cl', 'clang-cl')
    if msvc:
        flags = ['/nologo', '/std:c11', '/W4', '/WX', '/D_CRT_SECURE_NO_WARNINGS']
        flags += ['/I' + str(p) for p in includes]
        flags += ['/Fe:' + str(output), '/Fo:' + str(output.parent) + os.sep]
    else:
        flags = ['-std=c99', '-Wall', '-Wextra', '-Werror']
        flags += ['-I' + str(p) for p in includes] + ['-o', str(output)]
    flags += shlex.split(os.environ.get('HOST_CFLAGS', ''))
    result = subprocess.run(command + flags + [str(p) for p in sources], env=env,
                            cwd=output.parent, capture_output=True, text=True,
                            errors='replace', timeout=120)
    if result.returncode:
        raise RuntimeError('Host compile failed:\n' + result.stdout + result.stderr)
    version = subprocess.run(command if msvc else command + ['--version'], env=env,
                             capture_output=True, text=True, errors='replace', timeout=30)
    return {'command': command + flags, 'version': (version.stdout + version.stderr).strip().splitlines()[:3]}

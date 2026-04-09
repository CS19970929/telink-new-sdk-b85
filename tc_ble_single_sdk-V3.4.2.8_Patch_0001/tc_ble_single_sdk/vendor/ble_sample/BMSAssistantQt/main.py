from __future__ import annotations

import os
import sys
from pathlib import Path


_DLL_DIRECTORY_HANDLES = []


def configure_qt_runtime_paths() -> None:
    if not getattr(sys, "frozen", False):
        return None

    bundle_root = Path(getattr(sys, "_MEIPASS", Path(sys.executable).resolve().parent))
    pyside_dir = bundle_root / "PySide6"
    plugins_dir = pyside_dir / "plugins"
    platforms_dir = plugins_dir / "platforms"

    if pyside_dir.exists():
        os.environ["PATH"] = str(pyside_dir) + os.pathsep + os.environ.get("PATH", "")
        if hasattr(os, "add_dll_directory"):
            _DLL_DIRECTORY_HANDLES.append(os.add_dll_directory(str(pyside_dir)))
    if plugins_dir.exists():
        os.environ["QT_PLUGIN_PATH"] = str(plugins_dir)
        if hasattr(os, "add_dll_directory"):
            _DLL_DIRECTORY_HANDLES.append(os.add_dll_directory(str(plugins_dir)))
    if platforms_dir.exists():
        os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(platforms_dir)
        if hasattr(os, "add_dll_directory"):
            _DLL_DIRECTORY_HANDLES.append(os.add_dll_directory(str(platforms_dir)))
    if sys.platform == "win32":
        os.environ["QT_QPA_PLATFORM"] = "windows"

    return plugins_dir if plugins_dir.exists() else None


def main() -> int:
    plugins_dir = configure_qt_runtime_paths()

    from PySide6.QtCore import QCoreApplication
    from PySide6.QtWidgets import QApplication

    if plugins_dir is not None:
        QCoreApplication.addLibraryPath(str(plugins_dir))

    from bmsassistantqt.ui.main_window import MainWindow

    app = QApplication(sys.argv)
    app.setApplicationName("BMSAssistantQt")
    app.setOrganizationName("cs")
    app.setStyle("Fusion")

    window = MainWindow()
    if "--smoke-test" in sys.argv:
        print(window.windowTitle())
        return 0

    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())

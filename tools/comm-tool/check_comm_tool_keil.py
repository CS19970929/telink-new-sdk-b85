"""Check uVision multi-target membership without compiling or accessing hardware."""
from __future__ import annotations
import os
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET
from build_comm_tool import PROJECT, ROOT, prepare

TARGETS = ("COMM_TOOL_Release", "COMM_TOOL_IAP")


def inventory(target):
    return [(g.findtext("GroupName"), [f.findtext("FilePath").replace("\\", "/").casefold()
            for f in g.findall("./Files/File")]) for g in target.findall("./Groups/Group")]


def active_files(target):
    result = []
    for group in target.findall("./Groups/Group"):
        if group.findtext("./GroupOption/CommonProperty/IncludeInBuild") == "0":
            continue
        for file in group.findall("./Files/File"):
            if (file.findtext("FileType") in ("1", "2") and
                    file.findtext("./FileOption/CommonProperty/IncludeInBuild") != "0"):
                result.append(file.findtext("FilePath").replace("\\", "/").casefold())
    return result


class KeilProjectTests(unittest.TestCase):
    def setUp(self):
        self.targets = ET.parse(PROJECT).findall("./Targets/Target")

    def test_identical_ordered_file_tree_across_targets(self):
        self.assertEqual([t.findtext("TargetName") for t in self.targets], list(TARGETS))
        # uVision merges mismatched trees when opening/saving a multi-target project.
        self.assertEqual(inventory(self.targets[0]), inventory(self.targets[1]))
        for target in self.targets:
            groups = inventory(target)
            self.assertEqual(len(groups), len({g[0] for g in groups}))
            paths = [p for _, files in groups for p in files]
            self.assertEqual(len(paths), len(set(paths)))

    def test_target_entrypoints_and_common_sources(self):
        for target in self.targets:
            with self.subTest(target=target.findtext("TargetName")):
                files = active_files(target)
                names = [Path(p).name for p in files]
                is_app = target.findtext("TargetName") == TARGETS[0]
                self.assertEqual(names.count("main.c"), int(is_app))
                self.assertEqual(names.count("ct_iap_main.c"), int(not is_app))
                self.assertEqual(names.count("ct_app.c"), int(is_app))
                self.assertEqual(names.count("ct_iap.c"), int(not is_app))
                self.assertEqual(names.count("board_owc.c"), int(is_app))
                for shared in ("startup_stm32f10x_hd.s", "startup_default_handler.c",
                               "ct_watchdog.c", "stm32f10x_it.c", "system_stm32f10x.c"):
                    self.assertEqual(names.count(shared), 1, shared)

    def test_prepare_keeps_both_targets_and_compile_membership(self):
        parent = Path(os.environ.get("LOCALAPPDATA", tempfile.gettempdir())) / "CodexTemp/comm-tool"
        parent.mkdir(parents=True, exist_ok=True)
        expected = {t.findtext("TargetName"): active_files(t) for t in self.targets}
        for chosen in TARGETS:
            with self.subTest(target=chosen), tempfile.TemporaryDirectory(dir=parent) as temp:
                output = Path(temp)
                path = prepare(chosen, output)
                prepared = ET.parse(path).findall("./Targets/Target")
                self.assertEqual([t.findtext("TargetName") for t in prepared], list(TARGETS))
                self.assertEqual(inventory(prepared[0]), inventory(prepared[1]))
                outputs = []
                for target in prepared:
                    name = target.findtext("TargetName")
                    files = [str((PROJECT.parent / p).resolve()).replace("\\", "/").casefold()
                             for p in expected[name]]
                    self.assertEqual(active_files(target), files)
                    common = target.find("./TargetOption/TargetCommonOption")
                    self.assertEqual(common.findtext("CreateHexFile"), "0")
                    for section in ("BeforeMake", "AfterMake", "BeforeCompile"):
                        for node in common.findall(section + "/*"):
                            if node.tag.startswith("RunUserProg"):
                                self.assertEqual(node.text, "0")
                    dest = Path(common.findtext("OutputDirectory").rstrip("\\"))
                    self.assertIn(output, dest.parents)
                    self.assertNotIn(ROOT, dest.parents)
                    outputs.append(dest)
                self.assertNotEqual(*outputs)


if __name__ == "__main__":
    unittest.main(verbosity=2)

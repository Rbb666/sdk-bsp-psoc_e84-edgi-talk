import importlib.util
import pathlib
import sys
import tempfile
import unittest


MODULE_PATH = pathlib.Path(__file__).parents[2] / "tools" / "check_stack_usage.py"
SPEC = importlib.util.spec_from_file_location("check_stack_usage", MODULE_PATH)
check_stack_usage = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = check_stack_usage
SPEC.loader.exec_module(check_stack_usage)


class StackUsageTests(unittest.TestCase):
    def test_parses_gcc_records_and_reports_oversized_frames(self):
        records = check_stack_usage.parse_stack_usage(
            "uigame.c:1796:1:PAL_PlayerStatus\t130608\tstatic\n"
            "global.c:1470:1:PAL_InitGameData\t48\tstatic\n"
        )

        self.assertEqual(records[0].function, "PAL_PlayerStatus")
        self.assertEqual(records[0].bytes, 130608)
        self.assertEqual(
            check_stack_usage.validate_records(records, 12288),
            ["PAL_PlayerStatus uses 130608 bytes (limit 12288)"],
        )

    def test_rejects_unbounded_dynamic_stack_usage(self):
        records = check_stack_usage.parse_stack_usage(
            "script.c:10:1:PAL_RunTriggerScript\t64\tdynamic\n"
            "play.c:20:1:PAL_GameMain\t96\tdynamic,bounded\n"
        )

        self.assertEqual(
            check_stack_usage.validate_records(records, 12288),
            ["PAL_RunTriggerScript has unbounded dynamic stack usage"],
        )

    def test_rejects_directory_without_reports(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            with self.assertRaisesRegex(ValueError, "no GCC stack-usage reports"):
                check_stack_usage.collect_stack_usage(
                    pathlib.Path(temporary_directory)
                )


if __name__ == "__main__":
    unittest.main()

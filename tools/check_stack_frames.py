"""PlatformIO post-link guard and standalone Xtensa compiler stack-frame regression.
Run: python tools/check_stack_frames.py [--build-dir .pio/build/cyd_ili9341]
Requires a build with -fstack-usage; checks compiler output, not host ABI estimates.
This per-function bound is not proof of total runtime stack depth; measure Serial h too.
"""
from pathlib import Path
import argparse
import re
import tempfile

ROOT = Path(__file__).resolve().parents[1] if "__file__" in globals() else Path.cwd()
UI_LIMIT = 1024
COPY_LIMIT = 256


def inspect_frames(build_dir):
    frames = []
    required = {"main.cpp", "dashboard.cpp", "live_data.cpp"}
    found = set()
    for path in Path(build_dir).rglob("*.su"):
        source = next((name for name in required if path.name.startswith(name + ".") or path.name == name[:-4] + ".su"), None)
        if source is None:
            continue
        found.add(source)
        for row in path.read_text(encoding="utf-8").splitlines():
            parts = row.rsplit("\t", 2)
            if len(parts) != 3:
                raise AssertionError(f"Unrecognized stack report: {path}: {row}")
            location, size, kind = parts
            function = re.sub(r"^.*?:\d+:\d+:", "", location)
            frames.append((source, function, int(size), kind))
    assert found == required, f"Missing .su reports for {required - found}; run pio run -e cyd_ili9341 with -fstack-usage"
    failures = []
    watched = {"loop": False, "tick": False, "show": False, "copy": False, "diagnostics": False}
    for source, function, size, kind in frames:
        limit = UI_LIMIT if source in ("main.cpp", "dashboard.cpp") else None
        if source == "live_data.cpp" and ("copy_snapshot(" in function or "diagnostics()" in function):
            limit = COPY_LIMIT
        if limit is not None and (size > limit or ("dynamic" in kind and "bounded" not in kind)):
            failures.append(f"{source}: {function}: {size} bytes ({kind}), limit {limit}")
        for key, match in (("loop", "void loop()"), ("tick", "dashboard::tick("), ("show", "dashboard::show("),
                           ("copy", "live_data::copy_snapshot("), ("diagnostics", "live_data::diagnostics()")):
            if match in function:
                watched[key] = True
    assert all(watched.values()), f"Missing required compiled functions: {watched}"
    assert not failures, "Stack frame regression:\n" + "\n".join(failures)
    return frames


def report(build_dir):
    frames = inspect_frames(build_dir)
    for source, function, size, kind in frames:
        if (source == "main.cpp" and "void loop()" in function) or any(
            name in function for name in ("dashboard::tick(", "dashboard::show(", "live_data::copy_snapshot(", "live_data::diagnostics()")
        ):
            print(f"STACK {function}: {size} bytes ({kind})")
    ui = [size for source, _, size, _ in frames if source in ("main.cpp", "dashboard.cpp")]
    print(f"PASS: Xtensa application UI frames <= {UI_LIMIT} bytes (max {max(ui)}), snapshot copy/diagnostics <= {COPY_LIMIT}; loop stack size unchanged.")


def self_test():
    with tempfile.TemporaryDirectory(prefix="cyd-frame-test-") as folder:
        folder = Path(folder)
        main = folder / "main.cpp.su"
        main.write_text("src/main.cpp:1:1:void loop()\t96\tstatic\n", encoding="utf-8")
        (folder / "dashboard.cpp.su").write_text("src/dashboard.cpp:1:1:void dashboard::tick(unsigned int)\t32\tstatic\n"
                                                  "src/dashboard.cpp:2:1:void dashboard::show(unsigned char)\t32\tstatic\n", encoding="utf-8")
        (folder / "live_data.cpp.su").write_text("src/live_data.cpp:1:1:void live_data::copy_snapshot(live_data::Snapshot&)\t32\tstatic\n"
                                                 "src/live_data.cpp:2:1:live_data::Diagnostics live_data::diagnostics()\t32\tstatic\n", encoding="utf-8")
        inspect_frames(folder)
        # Actual offending frame size from the SHA-matched crash ELF must be rejected.
        main.write_text("src/main.cpp:1:1:void loop()\t3440\tstatic\n", encoding="utf-8")
        try:
            inspect_frames(folder)
        except AssertionError as error:
            assert "Stack frame regression" in str(error)
        else:
            raise AssertionError("Old 3440-byte loop frame escaped regression guard")
    print("PASS: frame guard rejects original crash-sized loop frame.")


if "Import" in globals():
    Import("env")

    def after_link(source, target, env):
        report(Path(env.subst("$BUILD_DIR")))

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", after_link)
elif __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / ".pio/build/cyd_ili9341")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    report(args.build_dir)

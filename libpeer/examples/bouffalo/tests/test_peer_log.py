#!/usr/bin/env python3
"""Check actual libpeer INFO/WARN/ERROR macros use the BL618DG log hook."""
import pathlib
import subprocess
import tempfile

base = pathlib.Path(__file__).resolve().parent.parent
source = (base / "app/bl618dg_stubs.c").read_text()
start = source.index("void peer_log(")
function = source[start:source.index("\n}\n", start) + 3]
include = base.parents[1] / "src"
harness = '''
#include <stdarg.h>
#include "utils.h"
#define portTICK_PERIOD_MS 2U
static unsigned xTaskGetTickCount(void) { return 617; }
'''
harness += function
harness += '''
int main(void) {
    LOGI("state=%s", "connected");
    LOGW("timeout=%u", 5000U);
    LOGE("error=%d", -1);
}
'''
with tempfile.TemporaryDirectory(prefix="peer-log-") as temp:
    binary = pathlib.Path(temp) / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-DLOG_REDIRECT=1", "-I" + str(include),
                    "-x", "c", "-", "-o", str(binary)],
                   input=harness, text=True, check=True)
    result = subprocess.run([str(binary)], capture_output=True,
                            text=True, check=True)
    lines = result.stdout.splitlines()
    assert len(lines) == 3, result.stdout
    for line, level, message in zip(lines, ("INFO", "WARN", "ERROR"),
                                    ("state=connected", "timeout=5000", "error=-1")):
        assert line.startswith("[1234] " + level + " "), line
        assert line.endswith(message), line
print("BL618DG libpeer log redirect: PASS")

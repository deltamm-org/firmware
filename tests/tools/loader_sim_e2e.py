# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Henri Lavikainen

"""dmm-radio against loader_sim: update, try, confirm, rollback, a crashing
image, refusals, an interrupted upload, power cuts during the swap, a test
cut and a row of updates. Usage: loader_sim_e2e.py <loader_sim> <dmm-radio> <work dir>"""

import os
import random
import shutil
import struct
import subprocess
import sys
import time

SIM, RADIO, WORK = sys.argv[1], sys.argv[2], sys.argv[3]
FLASH = os.path.join(WORK, "flash.bin")
LINK = os.path.join(WORK, "radio")
SIM_LOG = os.path.join(WORK, "sim.log")
failures = []


def image(version, length=96 * 1024, seed=1):
    """A slot image: vector table, info block linked for 0x08005000, filler."""
    base = 0x08005000
    rnd = random.Random(seed)
    body = bytearray(rnd.getrandbits(8) for _ in range(length))
    struct.pack_into("<II", body, 0, 0x20016000, (base + 0x150 + 40) | 1)
    struct.pack_into("<IIII", body, 0x150, 0x494D4D44, 1, base, base + length)
    body[0x160:0x160 + 24] = version.encode().ljust(24, b"\0")
    path = os.path.join(WORK, version + ".bin")
    with open(path, "wb") as f:
        f.write(body)
    return path


class Sim:
    def __init__(self):
        self.proc = None

    def start(self, *extra, install=None):
        args = [SIM, "--flash", FLASH, "--link", LINK, "--confirm-after", "2", "--idle", "4", *extra]
        if install:
            args += ["--install", install]
        self.log = open(SIM_LOG, "a")
        self.proc = subprocess.Popen(args, stdout=self.log, stderr=subprocess.STDOUT)
        time.sleep(0.5)

    def cut(self):
        """A power cut: the process dies at once."""
        self.proc.kill()
        self.proc.wait()
        self.log.close()

    def stop(self):
        if self.proc:
            self.proc.terminate()
            self.proc.wait()
            self.log.close()


def radio(*args, timeout=120):
    r = subprocess.run([RADIO, *args, "--port", LINK], capture_output=True, text=True, timeout=timeout,
                       env=dict(os.environ, XDG_CONFIG_HOME=os.path.join(WORK, "config")))
    return r.returncode, r.stdout + r.stderr


def check(name, ok, detail=""):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        failures.append(name)
        print(detail)


def running(text):
    for line in text.splitlines():
        if line.startswith("Running:") or line.startswith("DeltaMM:"):
            return line.split()[2] if line.startswith("Running:") else line.split()[1]
    return None


def wait_for_log(text, after, seconds=20):
    deadline = time.time() + seconds
    while time.time() < deadline:
        with open(SIM_LOG) as f:
            if text in f.read()[after:]:
                return True
        time.sleep(0.05)
    return False


def log_size():
    return os.path.getsize(SIM_LOG) if os.path.exists(SIM_LOG) else 0


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(os.path.join(WORK, "config"))
    a, b, crash = image("1.0-a", seed=1), image("1.0-b", 120 * 1024, seed=2), image("1.0-crash", seed=3)
    c = image("1.0-c", 100 * 1024, seed=4)
    rc, text = radio("config", "backup-before-flash", "off")
    sim = Sim()
    sim.start(install=a)
    try:
        rc, text = radio("info")
        check("first start adopts the installed image", rc == 0 and running(text) == "1.0-a" and "confirmed" in text, text)

        rc, text = radio("update", b, "--try", "--wait", "20")
        check("update --try runs the new image on trial", rc == 0 and running(text) == "1.0-b" and "on trial" in text
              and "start 1 of 3" in text, text)
        rc, text = radio("confirm")
        check("confirm keeps it", rc == 0 and "image confirmed" in text, text)

        rc, text = radio("rollback", "--wait", "20")
        check("rollback brings the previous image back", rc == 0 and running(text) == "1.0-a" and "kept" in text, text)

        rc, text = radio("update", b, "--wait", "20")
        time.sleep(3)
        rc2, info = radio("info")
        check("an automatic update confirms itself", rc == 0 and running(info) == "1.0-b" and "image confirmed" in info, text + info)

        rc, text = radio("update", crash, "--try", "--wait", "20")
        check("a crashing image falls back", rc == 1 and running(text) == "1.0-b" and "without being confirmed" in text, text)

        rc, text = radio("update", a, "--test-refusals", "--wait", "20")
        check("bad chunk and wrong commit are refused", rc == 0 and "refused (good)" in text and "commit refused (good)" in text
              and running(text) == "1.0-b", text)

        # An upload killed half-way, then run again: it picks up the chunks
        # already there.
        sim.stop()
        sim.start("--op-delay", "100")
        mark = log_size()
        env = dict(os.environ, XDG_CONFIG_HOME=os.path.join(WORK, "config"))
        upload = subprocess.Popen([RADIO, "update", c, "--port", LINK, "--wait", "20"], stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, env=env)
        wait_for_log("loader mode", mark)
        time.sleep(4.5)
        upload.kill()
        upload.wait()
        sim.stop()
        sim.start()
        rc, text = radio("update", c, "--wait", "20")
        check("an interrupted upload resumes", rc == 0 and running(text) == "1.0-c" and "already there" in text, text)
        time.sleep(2.5)

        # Power cuts during the swap: twice, then the swap finishes.
        sim.stop()
        sim.start("--op-delay", "10")
        mark = log_size()
        upload = subprocess.Popen([RADIO, "update", b, "--try", "--port", LINK, "--wait", "60"], stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, env=env)
        wait_for_log("loader mode", mark, 30)
        mark = log_size()
        wait_for_log("] restart\n", mark, 30)
        time.sleep(1.0)
        sim.cut()
        upload.kill()
        upload.wait()
        sim.start("--op-delay", "10")
        time.sleep(1.0)
        sim.cut()
        sim.start()
        rc, text = radio("info")
        for _ in range(20):
            if rc == 0:
                break
            time.sleep(0.5)
            rc, text = radio("info")
        check("power cuts during the swap: the swap finishes", rc == 0 and running(text) == "1.0-b" and "on trial" in text, text)
        radio("confirm")

        rc, text = radio("update", a, "--swap-cut", "7", "--wait", "20")
        check("a test cut during the swap is resumed", rc == 0 and running(text) == "1.0-a", text)

        ok = True
        for i in range(8):
            rc, text = radio("update", a if i % 2 else b, "--wait", "20")
            time.sleep(2.2)
            ok = ok and rc == 0 and running(text) == ("1.0-a" if i % 2 else "1.0-b")
        rc, info = radio("info")
        check("eight updates in a row", ok and "image confirmed" in info, text + info)

        rc, text = radio("loader")
        check("loader mode on request", rc == 0 and "Waiting:  asked for" in text, text)
        time.sleep(6)
        rc, text = radio("info")
        check("loader mode ends by itself", rc == 0 and running(text) == "1.0-a", text)
    finally:
        sim.stop()
    if failures:
        print("sim log:")
        with open(SIM_LOG) as f:
            print(f.read()[-4000:])
    return 1 if failures else 0


sys.exit(main())

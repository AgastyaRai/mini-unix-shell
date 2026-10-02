#!/usr/bin/env python3
"""race tests for fg, made repeatable with an injected delay.

The two timing bugs fixed in fg each lived in a gap of a few microseconds, so
the other tests pass on the broken code as well. This test runs crash inside a
pseudo-terminal with delay_sigcont.so preloaded (LD_PRELOAD). That library
replaces kill() with a version that waits 200 ms after sending SIGCONT. crash
itself is unchanged, but the gaps around continuing a job become wide enough
that the old code fails every time:

  1. fg sent SIGCONT before handing over the terminal, so a job that reads the
     terminal straight away got SIGTTIN and stopped again
  2. fg marked the job as running after SIGCONT without blocking SIGCHLD, so if
     the job ended in that gap, the stale update made fg wait forever
"""

import os
import pty
import re
import select
import signal
import subprocess
import sys
import time

BIN = "./crash"
SHIM = "./delay_sigcont.so"
TIMEOUT = 5.0

CTRL_D = "\x04"
CTRL_Z = "\x1a"

passed = 0
failed = 0


class Shell:
    """one crash process in its own pseudo-terminal, with the delay library preloaded"""

    def __init__(self):
        env = dict(os.environ, LD_PRELOAD=os.path.abspath(SHIM))
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execve(BIN, [BIN], env)
        self.buffer = ""

    def read_until(self, pattern, timeout=TIMEOUT):
        # read terminal output until the pattern shows up, and drop everything up to the match
        regex = re.compile(pattern)
        deadline = time.time() + timeout
        while True:
            match = regex.search(self.buffer)
            if match:
                self.buffer = self.buffer[match.end():]
                return match
            remaining = deadline - time.time()
            if remaining <= 0:
                return None
            ready, _, _ = select.select([self.fd], [], [], min(remaining, 0.1))
            if ready:
                try:
                    data = os.read(self.fd, 4096)
                except OSError:  # crash exited and the pty closed
                    return None
                if not data:
                    return None
                self.buffer += data.decode(errors="replace")

    def send(self, text):
        os.write(self.fd, text.encode())

    def close(self):
        # make sure nothing is left running, even if crash got stuck
        try:
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
        except (ProcessLookupError, ChildProcessError):
            pass
        os.close(self.fd)


def check(shell, message, pattern):
    global passed, failed
    match = shell.read_until(pattern)
    if match:
        print("PASS: " + message)
        passed += 1
    else:
        print("FAIL: " + message)
        failed += 1
    return match


def scenario_terminal_first():
    print("[RUN] Scenario 1: fg on a job that reads the terminal straight away")
    shell = Shell()
    try:
        shell.read_until(r"crash> ")
        shell.send("cat\n")
        time.sleep(0.5)
        shell.send(CTRL_Z)
        job = check(shell, "cat is suspended",
                    r"\[(\d+)\] \(\d+\)  suspended  cat")
        shell.read_until(r"crash> ")

        if job:
            shell.send("fg %" + job.group(1) + "\n")
            # wait out the injected delay before typing
            time.sleep(0.5)
            shell.send("hello-race\n")
            # the first copy is the terminal echoing what we typed; the second
            # is cat printing it back, which only happens if cat owns the terminal
            shell.read_until(r"hello-race")
            check(shell, "after fg, cat reads the terminal instead of stopping again",
                  r"hello-race")
            shell.send(CTRL_D)
            check(shell, "cat exits normally at end of input", r"finished  cat")
    finally:
        shell.close()


def scenario_job_ends_during_fg():
    print("[RUN] Scenario 2: fg on a job that ends as soon as it continues")
    shell = Shell()
    try:
        shell.read_until(r"crash> ")
        shell.send("sleep 30\n")
        time.sleep(0.5)
        shell.send(CTRL_Z)
        job = check(shell, "sleep is suspended",
                    r"\[(\d+)\] \((\d+)\)  suspended  sleep")
        shell.read_until(r"crash> ")

        if job:
            # a stopped process holds on to SIGTERM until it's continued,
            # so sleep dies the moment fg sends SIGCONT
            os.kill(int(job.group(2)), signal.SIGTERM)
            shell.send("fg %" + job.group(1) + "\n")
            check(shell, "crash reports the job as killed by SIGTERM", r"killed  15  sleep")
            check(shell, "fg returns to the prompt instead of waiting forever", r"crash> ")
    finally:
        shell.close()


def main():
    print("[BUILD] Compiling crash and the delay library...")
    subprocess.run(["make", "crash", "delay_sigcont.so"], check=True,
                   stdout=subprocess.DEVNULL)

    scenario_terminal_first()
    scenario_job_ends_during_fg()

    print()
    total = passed + failed
    if failed == 0:
        print(f"RESULT (race): ALL TESTS PASSED ({passed}/{total})")
        return 0
    print(f"RESULT (race): {failed} TEST(S) FAILED, {passed} PASSED")
    return 1


if __name__ == "__main__":
    sys.exit(main())

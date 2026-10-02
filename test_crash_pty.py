#!/usr/bin/env python3
"""job control tests that run crash inside a real pseudo-terminal.

The shell-script tests feed crash through a pipe or FIFO, so there is no
terminal: Ctrl-Z and Ctrl-C never come from the keyboard, and handing the
terminal to a job (tcsetpgrp) is never exercised. Here crash gets a pty as its
controlling terminal, and the test types the control characters itself.
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
TIMEOUT = 5.0

CTRL_C = "\x03"
CTRL_D = "\x04"
CTRL_Z = "\x1a"


def main():
    print("[BUILD] Compiling crash...")
    subprocess.run(["make", "crash"], check=True, stdout=subprocess.DEVNULL)

    # pty.fork makes the child a session leader with the pty as its terminal,
    # the same position a shell is in when you open a terminal window
    pid, fd = pty.fork()
    if pid == 0:
        os.execv(BIN, [BIN])

    buffer = ""
    passed = 0
    failed = 0

    def read_until(pattern, timeout=TIMEOUT):
        # read terminal output until the pattern shows up, and drop everything up to the match
        nonlocal buffer
        regex = re.compile(pattern)
        deadline = time.time() + timeout
        while True:
            match = regex.search(buffer)
            if match:
                buffer = buffer[match.end():]
                return match
            remaining = deadline - time.time()
            if remaining <= 0:
                return None
            ready, _, _ = select.select([fd], [], [], min(remaining, 0.1))
            if ready:
                try:
                    data = os.read(fd, 4096)
                except OSError:  # crash exited and the pty closed
                    return None
                if not data:
                    return None
                buffer += data.decode(errors="replace")

    def send(text):
        os.write(fd, text.encode())

    def check(message, pattern):
        nonlocal passed, failed
        match = read_until(pattern)
        if match:
            print("PASS: " + message)
            passed += 1
        else:
            print("FAIL: " + message)
            failed += 1
        return match

    def prompt():
        return read_until(r"crash> ")

    try:
        prompt()

        print("[RUN] Scenario 1: Ctrl-Z and Ctrl-C from the terminal")
        send("sleep 30\n")
        time.sleep(0.5)
        send(CTRL_Z)
        job = check("Ctrl-Z suspends the foreground job",
                    r"\[(\d+)\] \(\d+\)  suspended  sleep")
        prompt()

        if job:
            send("fg %" + job.group(1) + "\n")
            check("fg continues it in the foreground", r"continued  sleep")
            time.sleep(0.3)
            send(CTRL_C)
            check("Ctrl-C kills the foreground job", r"killed  sleep")
            prompt()

        print("[RUN] Scenario 2: fg on a job that reads the terminal")
        send("cat\n")
        time.sleep(0.5)
        send(CTRL_Z)
        job = check("a foreground cat can be suspended",
                    r"\[(\d+)\] \(\d+\)  suspended  cat")
        prompt()

        if job:
            send("fg %" + job.group(1) + "\n")
            check("fg continues cat", r"continued  cat")
            time.sleep(0.3)
            send("hello-from-pty\n")
            # the first copy is the terminal echoing what we typed; the second
            # is cat printing it back, which only happens if cat owns the terminal
            read_until(r"hello-from-pty")
            check("after fg, cat reads the terminal instead of stopping again",
                  r"hello-from-pty")
            send(CTRL_D)
            check("cat exits normally at end of input", r"finished  cat")
            prompt()

        print("[RUN] Scenario 3: quit")
        send("quit\n")
        deadline = time.time() + TIMEOUT
        status = None
        while time.time() < deadline:
            done, status = os.waitpid(pid, os.WNOHANG)
            if done:
                break
            time.sleep(0.05)
        if status is not None and os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0:
            print("PASS: crash exits cleanly on quit")
            passed += 1
        else:
            print("FAIL: crash did not exit cleanly on quit")
            failed += 1
    finally:
        # make sure nothing is left running if a check failed part-way
        try:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        except (ProcessLookupError, ChildProcessError):
            pass
        os.close(fd)

    print()
    total = passed + failed
    if failed == 0:
        print(f"RESULT (pty): ALL TESTS PASSED ({passed}/{total})")
        return 0
    print(f"RESULT (pty): {failed} TEST(S) FAILED, {passed} PASSED")
    return 1


if __name__ == "__main__":
    sys.exit(main())

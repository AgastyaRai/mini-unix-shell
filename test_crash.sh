#!/bin/sh
# simple regression test script for crash shell

# -e: exit on first error
# -u: treat unset variables as errors
set -eu

BIN=./crash

echo "[BUILD] Compiling crash..."

# send the make output to /dev/null to reduce noise
make crash >/dev/null

echo "[RUN] Scenario: two background jobs + nuke %1"
{
    # start two background sleep jobs
    echo "sleep 5 &"
    echo "sleep 5 &"

    # list jobs so we can see them as 'running'
    echo "jobs"

    # kill job %1
    echo "nuke %1"

    # give crash time to deliver SIGKILL + process SIGCHLD
    sleep 1

    # list jobs again after nuke
    echo "jobs"

    # exit the shell cleanly
    echo "quit"
} | "$BIN" > test_out.txt 2> test_err.txt
CRASH_STATUS=$?

echo
echo "==== crash stdout ===="
cat test_out.txt
echo "======================"
echo

# show stderr if there was any
if [ -s test_err.txt ]; then
    echo "[WARN] stderr not empty (may be fine depending on scenario):"
    cat test_err.txt
    echo
fi

# warn if crash exited non-zero
if [ "$CRASH_STATUS" -ne 0 ]; then
    echo "[WARN] crash exited with status $CRASH_STATUS (expected 0)"
fi

PASS=0
FAIL=0

# assert that a file contains a fixed string at least once
assert_contains() {
    pattern="$1"
    file="$2"
    msg="$3"

    # check if the specific/exact pattern exists in the file using grep -F
    if grep -F "$pattern" "$file" >/dev/null 2>&1; then
        echo "PASS: $msg"
        PASS=$((PASS+1))
    else
        echo "FAIL: $msg"
        FAIL=$((FAIL+1))
    fi
}

# assert that a file contains a fixed string at least N times
assert_count_at_least() {
    pattern="$1"
    file="$2"
    min="$3"
    msg="$4"

    # grep exits 1 if no match; we swallow that and just count lines
    # we count lines using wc -l and trim the accompanying whitespace with tr -d ' '
    count=$(grep -F "$pattern" "$file" 2>/dev/null | wc -l | tr -d ' ')
    # compare count against min using -ge
    if [ "$count" -ge "$min" ]; then
        echo "PASS: $msg (found $count ≥ $min)"
        PASS=$((PASS+1))
    else
        echo "FAIL: $msg (found $count < $min)"
        FAIL=$((FAIL+1))
    fi
}

# CHECKS

# at least one job reached 'running  sleep'
assert_contains "  running  sleep" test_out.txt \
    "background jobs are reported as 'running  sleep'"

# both sleeps should have been started
assert_count_at_least "  running  sleep" test_out.txt 2 \
    "two background sleep jobs reached the running state"

# nuke %1 should eventually produce a killed message
assert_contains "  killed  sleep" test_out.txt \
    "nuke %1 produced a 'killed  sleep' message"

echo
TOTAL=$((PASS + FAIL))
if [ "$FAIL" -eq 0 ]; then
    echo "RESULT: ALL TESTS PASSED ($PASS/$TOTAL)"
    exit 0
else
    echo "RESULT: $FAIL TEST(S) FAILED, $PASS PASSED"
    exit 1
fi

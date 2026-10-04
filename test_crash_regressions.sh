#!/bin/sh
# regression test script for crash: bugs found in a code review
#   1. the job limit counted every command ever run, not live jobs
#   2. a non-zero exit status was printed with no space before the command name
#   3. a line with more than 1024 words overflowed the token array
#   4. nuke only killed the job's first process, not the rest of its process group

# -u: treat unset variables as errors (no -e: each scenario reports its own result)
set -u

BIN=./crash
OUT=test_regressions_out.txt
ERR=test_regressions_err.txt

echo "[BUILD] Compiling crash..."

# send the make output to /dev/null to reduce noise
make crash >/dev/null

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

# assert that a file does not contain a fixed string
assert_not_contains() {
    pattern="$1"
    file="$2"
    msg="$3"

    # same check as above, but a match means the test failed
    if grep -F "$pattern" "$file" >/dev/null 2>&1; then
        echo "FAIL: $msg"
        FAIL=$((FAIL+1))
    else
        echo "PASS: $msg"
        PASS=$((PASS+1))
    fi
}

echo "[RUN] Scenario 1: more than 32 commands in one session"
{
    i=0
    while [ "$i" -lt 40 ]; do
        echo "true"
        i=$((i+1))
    done
    echo "sleep 1 &"
    echo "quit"
} | "$BIN" >"$OUT" 2>"$ERR"

assert_not_contains "too many jobs" "$ERR" \
    "40 finished commands don't use up the 32 job slots"
assert_contains "running  sleep" "$OUT" \
    "a background job still starts after 40 commands"

echo "[RUN] Scenario 2: a command that exits with a non-zero status"
{
    echo "false"
    echo "quit"
} | "$BIN" >"$OUT" 2>"$ERR"

assert_contains "finished  1  false" "$OUT" \
    "the exit status and command name are separated by two spaces"

echo "[RUN] Scenario 3: a line with 1,100 words"
{
    printf 'echo'
    i=0
    while [ "$i" -lt 1100 ]; do
        printf ' a'
        i=$((i+1))
    done
    printf '\n'
    echo "echo still-alive"
    echo "quit"
} | "$BIN" >"$OUT" 2>"$ERR"
CRASH_STATUS=$?

assert_contains "ERROR: too many arguments" "$ERR" \
    "an over-long line is rejected with an error"
assert_contains "still-alive" "$OUT" \
    "crash keeps running after rejecting the line"
if [ "$CRASH_STATUS" -eq 0 ]; then
    echo "PASS: crash exits cleanly (status 0)"
    PASS=$((PASS+1))
else
    echo "FAIL: crash exited with status $CRASH_STATUS"
    FAIL=$((FAIL+1))
fi

echo "[RUN] Scenario 4: nuke reaches the processes a job started"

# a job that starts a child of its own, which stays in the job's process group
SPAWNER=$(mktemp)
printf '#!/bin/sh\nsleep 4242 &\nwait\n' >"$SPAWNER"
chmod +x "$SPAWNER"

# print the PIDs of any 'sleep 4242' processes
find_grandchildren() {
    ps -eo pid=,args= | awk '$2 == "sleep" && $3 == "4242" {print $1}'
}

{
    echo "$SPAWNER &"
    # give the job time to start its child
    echo "sleep 1"
    echo "nuke %1"
    # give the kill time to land
    echo "sleep 1"
    echo "quit"
} | "$BIN" >"$OUT" 2>"$ERR"

LEFTOVER=$(find_grandchildren)
if [ -z "$LEFTOVER" ]; then
    echo "PASS: nuke %1 also killed the job's child process"
    PASS=$((PASS+1))
else
    echo "FAIL: nuke %1 left the job's child running (PID $LEFTOVER)"
    FAIL=$((FAIL+1))
    for p in $LEFTOVER; do
        kill "$p" 2>/dev/null
    done
fi

rm -f "$SPAWNER"

echo
TOTAL=$((PASS + FAIL))
if [ "$FAIL" -eq 0 ]; then
    echo "RESULT (regressions): ALL TESTS PASSED ($PASS/$TOTAL)"
    exit 0
else
    echo "RESULT (regressions): $FAIL TEST(S) FAILED, $PASS PASSED"
    exit 1
fi

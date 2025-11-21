#!/bin/sh
# Regression test script for crash: foreground/background jobs and suspend/resume behaviour.

# -e: exit on first error
# -u: treat unset variables as errors
set -eu

BIN=./crash
OUT=test_fg_bg_out.txt
ERR=test_fg_bg_err.txt
FIFO=crash_in

echo "[BUILD] Compiling crash..."

# send the make output to /dev/null to reduce noise
make crash >/dev/null

# remove any previous files or pipes
rm -f "$OUT" "$ERR" "$FIFO"

# make a named pipe (FIFO) so we can feed commands into crash
mkfifo "$FIFO"

echo "[RUN] Starting crash with FIFO as stdin..."
# start crash in the background:
#   - stdin: comes from the FIFO
#   - stdout: redirected to $OUT
#   - stderr: redirected to $ERR
"$BIN" <"$FIFO" >"$OUT" 2>"$ERR" &
CRASH_PID=$!

# keep a writer for the FIFO open on file descriptor 3 for the duration of the test
exec 3>"$FIFO"

# helper function: send a single command line to crash via the FIFO.
send_cmd() {
    printf '%s\n' "$1" >&3
}

# helper function: find the PID of a child process of crash with a given  command name (e.g., "sleep")
# we poll a few times to give crash a chance to fork/exec the child process.
get_child_pid() {
    parent="$1"
    name="$2"
    tries=20

    while [ "$tries" -gt 0 ]; do
        # ps prints "PID PPID COMMAND"; awk selects the first line where:
        #   PPID == parent AND COMMAND == name
        pid=$(ps -o pid= -o ppid= -o comm= 2>/dev/null | \
            awk -v p="$parent" -v n="$name" '$2 == p && $3 == n {print $1; exit}')

        # If we found a matching PID, print it and return success.
        if [ -n "${pid:-}" ]; then
            echo "$pid"
            return 0
        fi

        # otherwise, wait a bit and try again.
        tries=$((tries - 1))
        sleep 0.1
    done

    # if we reach here no matching PID was found so we return 1
    return 1
}

echo "[TEST 1] fg resumes a suspended foreground job"

# start a foreground sleep (no '&'), so crash blocks waiting for this job.
send_cmd "sleep 30"
# give crash some time to fork/exec the sleep process.
sleep 0.5

# locate the sleep process that is a direct child of crash.
JOB1_PID=$(get_child_pid "$CRASH_PID" sleep || true)
if [ -z "${JOB1_PID:-}" ]; then
    echo "FAIL: could not find foreground sleep child for TEST 1"
    exec 3>&-
    kill "$CRASH_PID" 2>/dev/null || true
    exit 1
fi
echo "  foreground sleep PID = $JOB1_PID"

# suspend the foreground job by sending SIGTSTP (equivalent to pressing Ctrl+Z).
kill -TSTP "$JOB1_PID"
sleep 0.5

# ask crash to resume this job in the foreground using fg <PID>.
send_cmd "fg $JOB1_PID"
sleep 0.5

# terminate the resumed job with SIGINT so we don't have to wait 30 seconds.
kill -INT "$JOB1_PID" 2>/dev/null || true
sleep 0.5

echo "[TEST 2] bg resumes a suspended background job"

# start a background sleep job (&), so crash returns to the prompt immediately.
send_cmd "sleep 30 &"
sleep 0.5

# locate the background sleep child of crash.
JOB2_PID=$(get_child_pid "$CRASH_PID" sleep || true)
if [ -z "${JOB2_PID:-}" ]; then
    echo "FAIL: could not find background sleep child for TEST 2"
    exec 3>&-
    kill "$CRASH_PID" 2>/dev/null || true
    exit 1
fi
echo "  background sleep PID = $JOB2_PID"

# suspend the background job with SIGTSTP.
kill -TSTP "$JOB2_PID"
sleep 0.5

# tell crash to resume this job in the background using bg <PID>.
send_cmd "bg $JOB2_PID"
sleep 0.5

# kill the resumed background job with SIGKILL so it finishes during the test.
kill -KILL "$JOB2_PID" 2>/dev/null || true
sleep 0.5

# exit crash cleanly
send_cmd "quit"
sleep 0.2

# close the FIFO writer and wait for crash to exit
exec 3>&-
wait "$CRASH_PID" 2>/dev/null || true

echo
echo "==== crash stdout (fg/bg test) ===="
cat "$OUT"
echo "==================================="
echo

# show stderr if there was any (some warnings may be fine depending on scenario)
if [ -s "$ERR" ]; then
    echo "[WARN] stderr not empty:"
    cat "$ERR"
    echo
fi

PASS=0
FAIL=0

# helper assertion: check that a given file contains a substring at least once.
assert_contains() {
    pattern="$1"
    file="$2"
    msg="$3"

    # grep -F performs a fixed-string search (no regex). We redirect both
    # stdout and stderr to /dev/null because we only care about whether the
    # pattern exists, not the actual matched lines or error messages.
    if grep -F "$pattern" "$file" >/dev/null 2>&1; then
        echo "PASS: $msg"
        PASS=$((PASS+1))
    else
        echo "FAIL: $msg"
        FAIL=$((FAIL+1))
    fi
}

# for TEST 1, we expect the foreground job to:
#   - be suspended
#   - be continued by fg <PID>
#   - eventually be killed by SIGINT
assert_contains "($JOB1_PID)  suspended  sleep" "$OUT" \
    "TEST 1: foreground job is reported as suspended after SIGTSTP"

assert_contains "($JOB1_PID)  continued  sleep" "$OUT" \
    "TEST 1: fg <PID> resumes the suspended foreground job"

assert_contains "($JOB1_PID)  killed  sleep" "$OUT" \
    "TEST 1: resumed foreground job eventually terminates with 'killed' status"

# for TEST 2, we expect the background job to:
#   - be suspended
#   - be continued by bg <PID>
#   - eventually be killed by SIGKILL
assert_contains "($JOB2_PID)  suspended  sleep" "$OUT" \
    "TEST 2: background job is reported as suspended after SIGTSTP"

assert_contains "($JOB2_PID)  continued  sleep" "$OUT" \
    "TEST 2: bg <PID> resumes the suspended background job"

assert_contains "($JOB2_PID)  killed  sleep" "$OUT" \
    "TEST 2: resumed background job eventually terminates with 'killed' status"

echo
TOTAL=$((PASS + FAIL))
if [ "$FAIL" -eq 0 ]; then
    echo "RESULT (fg/bg): ALL TESTS PASSED ($PASS/$TOTAL)"
    exit 0
else
    echo "RESULT (fg/bg): $FAIL TEST(S) FAILED, $PASS PASSED"
    exit 1
fi

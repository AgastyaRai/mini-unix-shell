// test helper for test_crash_race.py: a replacement kill() that waits 200 ms after sending SIGCONT
// (it's loaded into crash with LD_PRELOAD, so crash itself is unchanged, but the gaps around
// continuing a job get wide enough that timing bugs there show up every time instead of rarely)

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>

static int (*realKill)(pid_t, int) = NULL;

// helper function that finds the real kill in the C library when this library is loaded
// (doing it here means we never call dlsym from inside one of crash's signal handlers)
__attribute__((constructor))
static void findRealKill() {
    realKill = (int (*)(pid_t, int))dlsym(RTLD_NEXT, "kill");
}

int kill(pid_t pid, int signalNumber) {
    // send the signal for real
    int result = realKill(pid, signalNumber);

    // wait 200 ms after a continue signal, starting the wait again if a signal handler interrupts it
    if (signalNumber == SIGCONT) {
        int savedErrno = errno;
        struct timespec wait = {0, 200000000};
        struct timespec left;

        while (nanosleep(&wait, &left) == -1 && errno == EINTR) {
            wait = left;
        }

        errno = savedErrno;
    }

    return result;
}

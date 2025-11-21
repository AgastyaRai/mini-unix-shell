#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAXLINE 1024

// global variables
int processCount = 0;
pid_t foregroundPID = -1;

// structs
struct job {
    int jobNumber;
    pid_t pid;
    bool running;
    bool stopped;
    char *commandName;
};


// data structures

// we'll use an array to store the jobs
struct job jobs[32];


static void signalMessage(int jobNumber, pid_t pid, char *commandName, int exitStatus, int value);
static int intToStringLength(int number, char *buffer, int bufferLength);
static int findJobIndexByJobNumber(int jobNumber);


void eval(const char **toks, bool bg) { // bg is true iff command ended with &
    assert(toks);
    if (*toks == NULL) return;

    if (strcmp(toks[0], "quit") == 0) {
        if (toks[1] != NULL) {
            const char *msg = "ERROR: quit takes no arguments\n";
            write(STDERR_FILENO, msg, strlen(msg));
            return;
        } else {
            exit(0);
        }
    }

    // check if the command is jobs
    if (strcmp(toks[0], "jobs") == 0) {

        // check if there are any arguments
        if (toks[1] == NULL) { 

            // mask the signals
            sigset_t mask;
            sigemptyset(&mask);
            sigaddset(&mask, SIGCHLD);
            sigprocmask(SIG_BLOCK, &mask, NULL);

            // print all the jobs
            for (int i = 0; i < 32; i++) {
                if (jobs[i].running) {
                    printf("[%d] (%d)  running  %s\n", jobs[i].jobNumber, jobs[i].pid, jobs[i].commandName);
                } else if (jobs[i].stopped) {
                    printf("[%d] (%d)  suspended  %s\n", jobs[i].jobNumber, jobs[i].pid, jobs[i].commandName);
                }
            }

            fflush(stdout);

            // unmask the signals
            sigprocmask(SIG_UNBLOCK, &mask, NULL);

            return;
        } else {
            const char *msg = "ERROR: jobs takes no arguments\n";
            write(STDERR_FILENO, msg, strlen(msg));

            fflush(stdout);
            return;
        }
    }

    // check if the command is nuke
    if (strcmp(toks[0], "nuke") == 0) {

        if (toks[1] == NULL) {

            // mask the signals
            sigset_t mask;
            sigemptyset(&mask);
            sigaddset(&mask, SIGCHLD);
            sigprocmask(SIG_BLOCK, &mask, NULL);

            // kill all the jobs
            for (int i = 0; i < 32; i++) {
                if (jobs[i].running || jobs[i].stopped) {
                    kill(jobs[i].pid, SIGKILL);
                }
            }

            // unmask the signals
            sigprocmask(SIG_UNBLOCK, &mask, NULL);

            return;
        } 

        // loop through the arguments
        for (int i = 1; toks[i] != NULL; i++) {

            // check if the argument is a job number or pid
            if (toks[i][0] == '%') {

                // check if the job number is valid
                int jobNumber = atoi(toks[i] + 1);

                bool validInteger = true;

                for (int j = 1; toks[i][j] != '\0'; j++) {
                    if (toks[i][j] < '0' || toks[i][j] > '9') {
                        validInteger = false;
                        break;
                    }
                }

                if (!validInteger || jobNumber < 1) {
                    char errorMessage[MAXLINE];
                    int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: bad argument for nuke: %s\n", toks[i]);
                    write(STDERR_FILENO, errorMessage, errorMessageLength);
                    fflush(stdout);
                    continue;
                }

                // mask the signals
                sigset_t mask;
                sigemptyset(&mask);
                sigaddset(&mask, SIGCHLD);
                sigprocmask(SIG_BLOCK, &mask, NULL);

                int jobIndex = findJobIndexByJobNumber(jobNumber);

                if (jobIndex == -1) {
                    char errorMessage[MAXLINE];
                    int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: no job %d\n", jobNumber);
                    write(STDERR_FILENO, errorMessage, errorMessageLength);
                    fflush(stdout);
                    sigprocmask(SIG_UNBLOCK, &mask, NULL);
                    continue;
                }

                // kill the job
                kill(jobs[jobIndex].pid, SIGKILL);

                // unmask the signals
                sigprocmask(SIG_UNBLOCK, &mask, NULL);

                // wait a bit
                usleep(1000);

                continue;
            }

            // we assume the argument is a PID
            pid_t pid = atoi(toks[i]);

            // check if the pid is valid
            bool validInteger = true;

            for (int j = 0; toks[i][j] != '\0'; j++) {
                if (toks[i][j] < '0' || toks[i][j] > '9') {
                    validInteger = false;
                    break;
                }
            }

            if (!validInteger || pid < 0) {
                char errorMessage[MAXLINE];
                int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: bad argument for nuke: %s\n", toks[i]);
                write(STDERR_FILENO, errorMessage, errorMessageLength);
                fflush(stdout);
                continue;
            }

            // try to find a matching job
            bool matchFound = false;

            for (int j = 0; j < 32; j++) {
                if ((jobs[j].running || jobs[j].stopped) && jobs[j].pid == pid) {
                    matchFound = true;
                    kill(jobs[j].pid, SIGKILL);
                    fflush(stdout);
                    break;
                }
            }

            if (!matchFound) {
                char errorMessage[MAXLINE];
                int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: no PID %d\n", pid);
                write(STDERR_FILENO, errorMessage, errorMessageLength);
                fflush(stdout);    
            }

        }

        return;
    }

    // check if the command is fg
    if (strcmp(toks[0], "fg") == 0) {

        // check how many arguments there are
        if (toks[1] == NULL) {
            char errorMessage[MAXLINE];
            int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: fg needs exactly one argument\n");
            write(STDERR_FILENO, errorMessage, errorMessageLength);
            fflush(stdout);
            return;
        } else if (toks[2] != NULL) {
            char errorMessage[MAXLINE];
            int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: fg needs exactly one argument\n");
            write(STDERR_FILENO, errorMessage, errorMessageLength);
            fflush(stdout);
            return;
        }

        // check if the argument is a job number
        if (toks[1][0] == '%') {

            // check if the job number is an integer
            bool validInteger = true;

            for (int i = 1; toks[1][i] != '\0'; i++) {
                if (toks[1][i] < '0' || toks[1][i] > '9') {
                    validInteger = false;
                    break;
                }
            }

            // check if the job number is valid
            int jobNumber = atoi(toks[1] + 1);

            if (!validInteger || jobNumber < 1) {
                char errorMessage[MAXLINE];
                int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: bad argument for fg: %s\n", toks[1]);
                write(STDERR_FILENO, errorMessage, errorMessageLength);
                fflush(stdout);
                return;
            }

            int jobIndex = findJobIndexByJobNumber(jobNumber);

            if (jobIndex == -1) {
                char errorMessage[MAXLINE];
                int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: no job %d\n", jobNumber);
                write(STDERR_FILENO, errorMessage, errorMessageLength);
                fflush(stdout);
                return;
            }

            // put the job in the foreground

            // check if the job was stopped
            if (jobs[jobIndex].stopped) {

                // send a continue signal
                kill(jobs[jobIndex].pid, SIGCONT);

                // mark the job as running again
                jobs[jobIndex].stopped = false;
                jobs[jobIndex].running = true;
            }

            // set the process group to the job's process group so it can receive signals
            foregroundPID = jobs[jobIndex].pid;
            tcsetpgrp(STDIN_FILENO, jobs[jobIndex].pid);

            // wait for the job to finish
            while (jobs[jobIndex].running) {
                usleep(1000);
            }

            // set the process group back to the shells process group
            tcsetpgrp(STDIN_FILENO, getpgid(0));
            foregroundPID = -1;
            return;
        } else {
            // we assume the argument is a PID
            pid_t pid = atoi(toks[1]);

            // check if the pid is valid
            bool validInteger = true;

            for (int j = 0; toks[1][j] != '\0'; j++) {
                if (toks[1][j] < '0' || toks[1][j] > '9') {
                    validInteger = false;
                    break;
                }
            }

            if (!validInteger || pid < 0) {
                char errorMessage[MAXLINE];
                int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: bad argument for fg: %s\n", toks[1]);
                write(STDERR_FILENO, errorMessage, errorMessageLength);
                fflush(stdout);
                return;
            }

            // try to find a matching job
            int jobIndex = -1;

            for (int j = 0; j < 32; j++) {
                if ((jobs[j].running || jobs[j].stopped) && jobs[j].pid == pid) {
                    jobIndex = j;
                    break;
                }
            }

            // check if we found a match
            if (jobIndex == -1) {
                char errorMessage[MAXLINE];
                int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: no PID %d\n", pid);
                write(STDERR_FILENO, errorMessage, errorMessageLength);
                fflush(stdout);
                return;
            }            

            // check if the job was stopped
            if (jobs[jobIndex].stopped) {
                // send a continue signal
                kill(jobs[jobIndex].pid, SIGCONT);

                // mark the job as running again
                jobs[jobIndex].stopped = false;
                jobs[jobIndex].running = true;
            }

            // put the job in the foreground

            // set the process group to the job's process group so it can receive signals
            foregroundPID = jobs[jobIndex].pid;
            tcsetpgrp(STDIN_FILENO, jobs[jobIndex].pid);

            // wait for the job to finish
            while (jobs[jobIndex].running) {
                usleep(1000);
            }

            // set the process group back to the shells process group
            tcsetpgrp(STDIN_FILENO, getpgid(0));
            foregroundPID = -1;
            return;
        }
    }

    // check if the command is bg
    if (strcmp(toks[0], "bg") == 0) {

        if (toks[1] == NULL) {
            const char *msg = "ERROR: bg needs some arguments\n";
            write(STDERR_FILENO, msg, strlen(msg));
            fflush(stdout);
            return;
        }

        // mask the signals
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, NULL);

        // process all the arguments
        for (int i = 1; toks[i] != NULL; i++) {

            // check if the argument is a job number
            if (toks[i][0] == '%') {

                // check if the job number is an integer
                bool validInteger = true;

                for (int j = 1; toks[i][j] != '\0'; j++) {
                    if (toks[i][j] < '0' || toks[i][j] > '9') {
                        validInteger = false;
                        break;
                    }
                }

                // check if the job number is valid
                int jobNumber = atoi(toks[i] + 1);

                if (!validInteger || jobNumber < 1 ) {
                    char errorMessage[MAXLINE];
                    int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: bad argument for bg: %s\n", toks[i]);
                    write(STDERR_FILENO, errorMessage, errorMessageLength);
                    fflush(stdout);
                    continue;
                }

                int jobIndex = findJobIndexByJobNumber(jobNumber);

                if (jobIndex == -1) {
                    char errorMessage[MAXLINE];
                    int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: no job %d\n", jobNumber);
                    write(STDERR_FILENO, errorMessage, errorMessageLength);
                    fflush(stdout);
                    continue;
                }

                // check if the job is stopped
                if (!jobs[jobIndex].stopped) {
                    continue;
                }

                // send a continue signal
                kill(jobs[jobIndex].pid, SIGCONT);

                // mark the job as running again
                jobs[jobIndex].stopped = false;
                jobs[jobIndex].running = true;

            } else {
                // we assume the argument is a PID
                pid_t pid = atoi(toks[i]);

                // check if the pid is valid
                bool validInteger = true;

                for (int j = 0; toks[i][j] != '\0'; j++) {
                    if (toks[i][j] < '0' || toks[i][j] > '9') {
                        validInteger = false;
                        break;
                    }
                }

                if (!validInteger || pid < 0) {
                    char errorMessage[MAXLINE];
                    int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: bad argument for bg: %s\n", toks[i]);
                    write(STDERR_FILENO, errorMessage, errorMessageLength);
                    fflush(stdout);
                    continue;
                }

                // try to find a matching job
                int jobIndex = -1;

                for (int j = 0; j < 32; j++) {
                    if ((jobs[j].running || jobs[j].stopped) && jobs[j].pid == pid) {
                        jobIndex = j;
                        break;
                    }
                }

                // check if we found a match
                if (jobIndex == -1) {
                    char errorMessage[MAXLINE];
                    int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: no PID %d\n", pid);
                    write(STDERR_FILENO, errorMessage, errorMessageLength);
                    fflush(stdout);
                    continue;
                }            

                // check if the job is stopped
                if (!jobs[jobIndex].stopped) {
                    continue;
                }

                // send a continue signal
                kill(jobs[jobIndex].pid, SIGCONT);

                // mark the job as running again
                jobs[jobIndex].stopped = false;
                jobs[jobIndex].running = true;
            }
        }

        // unmask the signals
        sigprocmask(SIG_UNBLOCK, &mask, NULL);

        return;
    }


    // check the process count
    if (processCount >= 32) {
        const char *msg = "ERROR: too many jobs\n";
        write(STDERR_FILENO, msg, strlen(msg));
        return;
    }

    processCount++;

    // block any incoming signals using sigprocmask
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    // fork the current process
    pid_t pid = fork();        

    if (pid == -1) {
        const char *msg = "ERROR: fork didn't work\n";
        write(STDERR_FILENO, msg, strlen(msg));
        processCount--;
        return;
    }


    if (pid != 0 && bg) {
        // parent process
        printf("[%d] (%d)  running  %s\n", processCount, pid, toks[0]);
        fflush(stdout);

        // prepare to add the job to the jobs array
        struct job newJob;
        newJob.jobNumber = processCount;
        newJob.pid = pid;
        newJob.running = true;
        newJob.stopped = false;
        newJob.commandName = strdup(toks[0]);
        
        // add the job to the jobs array
        for (int i = 0; i < 32; i++) {
            if (!jobs[i].running && !jobs[i].stopped) {
                jobs[i] = newJob;
                break;
            }
        }

        // unblock the signals
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        return;
        
    } else if (pid != 0 && !bg) {
        // parent process

        // add the job to the jobs array
        struct job newJob;
        newJob.jobNumber = processCount;
        newJob.pid = pid;
        newJob.running = true;
        newJob.stopped = false;
        newJob.commandName = strdup(toks[0]);
        
        int jobIndex = -1;

        for (int i = 0; i < 32; i++) {
            if (!jobs[i].running && !jobs[i].stopped) {
                jobs[i] = newJob;
                jobIndex = i;
                break;
            }
        }

        // unblock the signals
        sigprocmask(SIG_UNBLOCK, &mask, NULL);

        // create a new process group for the child process to differentiate between foreground processes for signals
        setpgid(pid, pid);
        foregroundPID = pid;
        // transfer control to the child process group
        tcsetpgrp(STDIN_FILENO, pid);

        // wait for the child process to finish
        while (jobIndex != -1 && jobs[jobIndex].running) {
            usleep(1000);
        }

        // set the process group back to the shells process group
        tcsetpgrp(STDIN_FILENO, getpgid(0));
        foregroundPID = -1;

        return;
    }
    
    // child process
    setpgid(0, 0);

    // unblock the signals
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    
    // reset the signal handlers
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);

    // execute the command
    int checkValue = execvp(toks[0], toks);

    if (checkValue == -1) {
        // print the error message
        char errorMessage[MAXLINE];
        int errorMessageLength = snprintf(errorMessage, sizeof(errorMessage), "ERROR: cannot run %s\n", toks[0]);
        write(STDERR_FILENO, errorMessage, errorMessageLength);
        exit(1);
    }

}


void parse_and_eval(char *s) {
    assert(s);
    const char *toks[MAXLINE+1];
    
    while (*s != '\0') {
        bool end = false;
        bool bg = false;
        int t = 0;

        while (*s != '\0' && !end) {
            while (*s == '\n' || *s == '\t' || *s == ' ') ++s;
            if (*s != ';' && *s != '&' && *s != '\0') toks[t++] = s;
            while (strchr("&;\n\t ", *s) == NULL) ++s;
            switch (*s) {
            case '&':
                bg = true;
                end = true;
                break;
            case ';':
                end = true;
                break;
            }
            if (*s) *s++ = '\0';
        }
        toks[t] = NULL;
        eval(toks, bg);
    }
}

void prompt() {
    const char *prompt = "crash> ";
    ssize_t nbytes = write(STDOUT_FILENO, prompt, strlen(prompt));
}

int repl() {
    char *buf = NULL;
    size_t len = 0;
    while (prompt(), getline(&buf, &len, stdin) != -1) {
        parse_and_eval(buf);
    }

    if (buf != NULL) free(buf);
    if (ferror(stdin)) {
        perror("ERROR");
        return 1;
    }
    return 0;
}


// function to handle sigint signals
void sigintHandler(int signal) {
    // function to handle sigint signals
    if (foregroundPID != -1) {
        kill(-foregroundPID, SIGINT);
    }
}


// function to handle sigquit signals
void sigquitHandler(int signal) {
    if (foregroundPID != -1) {
        kill(-foregroundPID, SIGQUIT);
    } else {
        // no foreground job: exit crash as per spec
        exit(0);
    }
}

// function to handle sigtstp signals
void sigtstpHandler(int signal) {
    if (foregroundPID != -1) {
        kill(-foregroundPID, SIGTSTP);
    }
}

// function to handle sigchild signals

void sigchildHandler(int signal) {
    pid_t pid;
    int status;

    while (true) {

        // send a signal to the process
        pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED);
        if (pid <= 0) {
            break;
        }

        // find the job with the same pid
        for (int i = 0; i < 32; i++) {
            if (jobs[i].pid == pid) {

                int jobNumber = jobs[i].jobNumber;
                char *commandName = jobs[i].commandName;

                if (WIFSTOPPED(status)) {
                    jobs[i].stopped = true;
                    jobs[i].running = false;
                    
                    signalMessage(jobNumber, pid, commandName, 2, -1);
                } else if (WIFEXITED(status)) {
                    jobs[i].running = false;
                    jobs[i].stopped = false;

                    int exitStatus = WEXITSTATUS(status);
                    signalMessage(jobNumber, pid, commandName, 0, exitStatus);

                } else if (WIFSIGNALED(status)) {
                    jobs[i].running = false;
                    jobs[i].stopped = false;

                    int signalNumber = WTERMSIG(status);

                    if (signalNumber == SIGKILL || signalNumber == SIGINT) {
                        signalMessage(jobNumber, pid, commandName, 1, -1);
                    } else {
                        signalMessage(jobNumber, pid, commandName, 1, signalNumber);
                    }
                } else if (WIFCONTINUED(status)) {
                    jobs[i].stopped = false;
                    jobs[i].running = true;

                    signalMessage(jobNumber, pid, commandName, 3, -1);
                } else {
                    jobs[i].running = false;
                    jobs[i].stopped = false;

                    signalMessage(jobNumber, pid, commandName, 0, -1);
                }

                break;
            }
        }
    }
}

// function to print the job finished text in a signal safe way

static void signalMessage(int jobNumber, pid_t pid, char *commandName, int exitStatus, int value) {

    char message[MAXLINE];
    char pidString[10];
    char jobNumberString[10];

    // convert numbers to strings
    int pidLength = intToStringLength(pid, pidString, 10);
    int jobNumberLength = intToStringLength(jobNumber, jobNumberString, 10);
    
    int messageIndex = 0;

    // build the message
    message[messageIndex++] = '[';

    // add the job number
    for (int i = 0; i < jobNumberLength; i++) {
        message[messageIndex++] = jobNumberString[i];
    }

    message[messageIndex++] = ']';
    message[messageIndex++] = ' ';

    message[messageIndex++] = '(';

    // add the pid
    for (int i = 0; i < pidLength; i++) {
        message[messageIndex++] = pidString[i];
    }

    message[messageIndex++] = ')';
    message[messageIndex++] = ' ';
    message[messageIndex++] = ' ';

    // check what the status is

    // normal exit
    if (exitStatus == 0) {
        const char *finishedString = "finished";
        for (int i = 0; finishedString[i] != '\0'; i++) {
            message[messageIndex++] = finishedString[i];
        }

        message[messageIndex++] = ' ';
        message[messageIndex++] = ' ';

        if (value > 0) {

            char valueString[10];
            int valueLength = intToStringLength(value, valueString, sizeof(valueString));
            for (int i = 0; i < valueLength; i++) {
                message[messageIndex++] = valueString[i];
            }
        }
    // signal termination
    } else if (exitStatus == 1) {
        const char *finishedString = "killed";
        for (int i = 0; finishedString[i] != '\0'; i++) {
            message[messageIndex++] = finishedString[i];
        }

        message[messageIndex++] = ' ';
        message[messageIndex++] = ' ';

        if (value > 0) {
            char valueString[10];
            int valueLength = intToStringLength(value, valueString, 10);
            for (int i = 0; i < valueLength; i++) {
                message[messageIndex++] = valueString[i];
            }
            message[messageIndex++] = ' ';
            message[messageIndex++] = ' ';
        }
    } else if (exitStatus == 2) {
        const char *finishedString = "suspended";
        for (int i = 0; finishedString[i] != '\0'; i++) {
            message[messageIndex++] = finishedString[i];
        }

        message[messageIndex++] = ' ';
        message[messageIndex++] = ' ';
    } else if (exitStatus == 3) {
        const char *finishedString = "continued";
        for (int i = 0; finishedString[i] != '\0'; i++) {
            message[messageIndex++] = finishedString[i];
        }

        message[messageIndex++] = ' ';
        message[messageIndex++] = ' ';
    }

    // add the command name
    for (int i = 0; i < strlen(commandName); i++) {
        message[messageIndex++] = commandName[i];
    }

    // add the newline
    message[messageIndex++] = '\n';

    // end the message
    message[messageIndex] = '\0';

    // write the message
    write(STDOUT_FILENO, message, messageIndex);
}

// function to get the length of an integer in string form in a signal safe way
static int intToStringLength(int number, char *buffer, int bufferLength) {
    // use mod to repeatedly get the last digit

    int i = 0;

    if (number == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return 1;
    }

    while (number > 0) {
        buffer[i] = number % 10 + '0';
        number = number / 10;
        i++;
    }

    buffer[i] = '\0';

    // reverse the string
    int length = i;

    for (int j = 0; j < length / 2; j++) {
        char temp = buffer[j];
        buffer[j] = buffer[length - j - 1];
        buffer[length - j - 1] = temp;
    }

    return length;
}

// helper function that returns job index from the jobNumber
static int findJobIndexByJobNumber(int jobNumber) {
    for (int i = 0; i < 32; i++) {
        if ((jobs[i].running || jobs[i].stopped) && jobs[i].jobNumber == jobNumber) {
            return i;
        }
    }
    return -1;
}



int main(int argc, char **argv) {

    signal(SIGTTOU, SIG_IGN);

    // set up the sigchild handler
    struct sigaction sigHandlerMessage;
    sigHandlerMessage.sa_handler = sigchildHandler;
    sigHandlerMessage.sa_flags = SA_RESTART;
    sigemptyset(&sigHandlerMessage.sa_mask);
    sigaction(SIGCHLD, &sigHandlerMessage, NULL);

    // set up the sigint handler
    struct sigaction sigHandlerInt;
    sigHandlerInt.sa_handler = sigintHandler;
    sigHandlerInt.sa_flags = SA_RESTART;
    sigemptyset(&sigHandlerInt.sa_mask);
    sigaction(SIGINT, &sigHandlerInt, NULL);

    // set up the sigquit handler
    struct sigaction sigHandlerQuit;
    sigHandlerQuit.sa_handler = sigquitHandler;
    sigHandlerQuit.sa_flags = SA_RESTART;
    sigemptyset(&sigHandlerQuit.sa_mask);
    sigaction(SIGQUIT, &sigHandlerQuit, NULL);

    // set up the sigtstp handler
    struct sigaction sigHandlerTstp;
    sigHandlerTstp.sa_handler = sigtstpHandler;
    sigHandlerTstp.sa_flags = SA_RESTART;
    sigemptyset(&sigHandlerTstp.sa_mask);
    sigaction(SIGTSTP, &sigHandlerTstp, NULL);

    return repl();
}

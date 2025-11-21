# mini-unix-shell
Implementation of a Unix-like shell with job control in C. 



### Running
To run the code, do:

```
make
./crash
```

It runs external programs with `fork`/`execvp`, background jobs with `&`, and has built-in support for `jobs`, `fg`, `bg`, `nuke` and `quit`, along with signal handling.


### Shell Scripts

This repo also contains simple shell scripts that test and demonstrate the job control features. 

Both scripts build `crash` and run a scripted session against it, printing out a small PASS/FAIL summary based on the expected output.

`test_crash.sh` starts two background `sleep` jobs, runs `nuke %1` and checks that both jobs reached the `running sleep` state and that at least one `killed sleep` appeared in the output.

`test_crash_fg_bg.sh` tests the suspend and resume behavior for foreground and background jobs. It suspends a foreground `sleep` with `SIGTSTP` (equivalent to pressing Ctrl+Z when we're using
`crash`), suspends a background `sleep` and resumes it with `bg <PID>`, and checks for the `suspended`, `continued` and `killed` messages for each PID.

To run them:

```
chmod +x test_crash.sh test_crash_fg_bg.sh

./test_crash.sh
./test_crash_fg_bg.sh
```

More thorough testing can (and should when making changes) be done by actually putting the inputs in directly as shown below.


### Example Inputs

Once you get `crash` running (see above), here are some simple tests you can do to verify the appropriate output. Your order of interruption (i.e. when you
get the `finished sleep` message) may vary depending on your typing speed, but other than that putting the instructions in in the same order should yield the
same results.

#### General Usage

```
crash> sleep 30 &
[1] (19887)  running  sleep
crash> jobs 
[1] (19887)  running  sleep
crash> sleep 20 &
[2] (20002)  running  sleep
crash> jobs
[1] (19887)  running  sleep
[2] (20002)  running  sleep
crash> sleep 30
^Z[3] (20055)  suspended  sleep
crash> jobs
[1] (19887)  running  sleep
[2] (20002)  running  sleep
[3] (20055)  suspended  sleep
crash> bg %3
crash> [3] (20055)  continued  sleep
jobs
[1] (19887)  running  sleep
[2] (20002)  running  sleep
[3] (20055)  running  sleep
crash> fg %1
^C[1] (19887)  killed  sleep
crash> jobs
[2] (20002)  running  sleep
[3] (20055)  running  sleep
crash> [2] (20002)  finished  sleep
nuke %3
[3] (20055)  killed  sleep
crash> jobs
crash> quit
```


#### Bad Inputs

```
crash> fg
ERROR: fg needs exactly one argument
crash> fg %99
ERROR: no job 99
crash> nuke 12x
ERROR: bad argument for nuke: 12x
```

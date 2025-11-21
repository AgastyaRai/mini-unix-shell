# mini-unix-shell
Implementation of a Unix-like shell with job control in C. 



### Running
To run the code, do:

```
make
./crash
```

It runs external programs with `fork`/`execvp`, background jobs with `&`, and has built-in support for `jobs`, `fg`, `bg`, `nuke` and `quit`, along with signal handling.



### Example Inputs

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

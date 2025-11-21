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




#### Bad Inputs

```
crash> fg
ERROR: fg needs exactly one argument
crash> fg %99
ERROR: no job 99
crash> nuke 12x
ERROR: bad argument for nuke: 12x
```

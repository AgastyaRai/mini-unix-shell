crash: crash.c
	$(CC) -o $@ $^

delay_sigcont.so: delay_sigcont.c
	$(CC) -shared -fPIC -o $@ $^ -ldl

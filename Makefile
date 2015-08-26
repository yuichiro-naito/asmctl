all: asmctl

asmctl: src/asmctl.c
	$(CC) $(CFLAGS) -o $@ $<

install: asmctl
	install -m 4755 asmctl /usr/local/bin

clean:
	rm -f asmctl

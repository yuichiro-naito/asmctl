all: asmctl

asmctl: src/asmctl.c
	$(CC) $(CFLAGS) -o $@ $<

install: asmctl
	install -m 4755 asmctl /usr/local/bin
	mkdir -p /usr/local/etc/devd
	install -m 644 devd/asmctl.conf /usr/local/etc/devd

clean:
	rm -f asmctl

prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
localstatedir = ${prefix}/var
sysconfdir = ${prefix}/etc
INSTALL = /usr/bin/install -c
SED = /usr/bin/sed
CC = cc
DEFS = -O -DHAVE_CONFIG_H
LIBS = 
INCS = -I.

CONF = devd/asmctl.conf
SRCS = src/asmctl.c
OBJS = $(SRCS:.c=.o)
PROG = asmctl

all: $(PROG) devd/asmctl.conf

$(PROG): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

.c.o:
	$(CC) $(INCS) $(DEFS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(PROG) devd/asmctl.conf

install: $(PROG) $(CONF)
	$(INSTALL) -d $(bindir)
	$(INSTALL) -m 4755 $(PROG) $(bindir)
	$(SED) -i .s -e "s|%%BINDIR%%|$(bindir)|" $(CONF)
	$(INSTALL) -d -m 644 $(sysconfdir)/devd
	$(INSTALL) -m 644 $(CONF) $(sysconfdir)/devd

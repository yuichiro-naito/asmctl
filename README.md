# asmctl
Apple System Management Controller

Asmctl is a command line tool for Apple System Management Controller,
which controls keyboard backlight and LCD backlight.

Asmctl uses sysctl variables of dev.asmc.0.* and hw.acpi.video.lcd0.*
which are provided by FreeBSD kernel.


HOW TO INSTALL

$ sudo make install

HOW TO USE

1. set video backlight more bright
   $ /usr/local/bin/asmctl video up

2. set video backlight less dark
   $ /usr/local/bin/asmctl video down

3. set keyboard backlight more bright
   $ /usr/local/bin/asmctl key up

4. set keyboard backlight less dark
   $ /usr/local/bin/asmctl key down

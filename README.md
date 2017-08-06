# asmctl
Apple System Management Controller

Asmctl is a command line tool for Apple System Management Controller,
which controls keyboard backlight and LCD backlight.

Asmctl uses sysctl variables of dev.asmc.0.* and hw.acpi.video.lcd0.*
which are provided by FreeBSD kernel.


## REQUIREMENT

Asmctl requires following two kernel modules.

 1. asmc
 1. acpi_video

To load these modules on boot,
write following sentences in `/boot/loader.conf`.

```
asmc_load="YES"
acpi_video_load="YES"
```

## HOW TO INSTALL

```
$ ./configure
$ make
$ make install
```

## HOW TO USE

1. set video backlight more bright

   ```
   $ /usr/local/bin/asmctl video up
   ```

2. set video backlight less dark

   ```
   $ /usr/local/bin/asmctl video down
   ```

3. set keyboard backlight more bright

   ```
   $ /usr/local/bin/asmctl key up
   ```

4. set keyboard backlight less dark

   ```
   $ /usr/local/bin/asmctl key down
   ```

Assigning following key bindings work similar to Apple Macbook series.

| key |      assign       |
|-----|-------------------|
| F1  | asmctl video down |
| F2  | asmctl video up   |
| F5  | asmctl key down   |
| F6  | asmctl key up     |

## FOLLOWING AC POWER STATUS

FreeBSD kernel has two acpi video brightness values.
One is for ac powered and the other is for battery powered.

Asmctl switches these two values specified by acpi ac power status.

```/usr/local/etc/devd/asmctl.conf``` makes FreeBSD devd
triggering ```asmctl video acpi```.

## SECURITY

Changing hw.acpi.video.* sysctl variables requires root privilege.
For this reason, asmctl is installed with setuid root.
On FreeBSD-11.0R or higher, asmctl uses capsicum(4) to be sandboxed.

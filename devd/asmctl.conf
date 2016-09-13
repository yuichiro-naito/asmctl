notify 20 {
	match "system"		"ACPI";
	match "subsystem"	"ACAD";
	action "/etc/rc.d/power_profile $notify";
	action "%%BINDIR%%/asmctl video acpi";
};

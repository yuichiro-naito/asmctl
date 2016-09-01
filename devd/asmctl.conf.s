notify 20 {
	match "system"		"ACPI";
	match "subsystem"	"ACAD";
	action "/etc/rc.d/power_profile $notify";
	action "%%EXEC_PREFIX%%/asmctl video acpi";
};

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([asmctl], [1.0], [naito.yuichiro@gmail.com])
AC_CONFIG_SRCDIR([src/asmctl.c])
AC_CONFIG_HEADERS([config.h])

AC_PREFIX_DEFAULT(/usr/local)

# Checks for programs.
AC_PROG_CC
AC_PROG_SED
AC_PROG_INSTALL

# Checks for libraries.
AC_CHECK_LIB(nv, FreeBSD_nvlist_create)
AC_CHECK_LIB(nv, nvlist_create)
AC_CHECK_LIB(casper, cap_init)
AC_CHECK_LIB(cap_sysctl, cap_sysctlbyname)

# Checks for header files.
AC_CHECK_HEADERS([unistd.h stdlib.h string.h fcntl.h nl_types.h])
AC_CHECK_HEADERS([sys/nv.h sys/capsicum.h libcasper.h capsicum_helpers.h])
AC_CHECK_HEADERS([sys/ioctl.h])

# Check for functions.
AC_CHECK_FUNCS([cap_sysctl_limit_name])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_REALLOC
AC_FUNC_MALLOC
AC_CHECK_FUNCS([strchr strtol strerror ftruncate])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

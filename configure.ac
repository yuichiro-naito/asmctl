#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([asmctl], [1.0], [naito.yuichiro@gmail.com])
AC_CONFIG_SRCDIR([src/asmctl.c])
AC_CONFIG_HEADERS([config.h])

test "$prefix" = NONE && prefix=$ac_default_prefix
AC_DEFINE_UNQUOTED(PREFIX, ["${prefix}"])

# Checks for programs.
AC_PROG_CC
AC_PROG_SED
AC_PROG_INSTALL

# Checks for libraries.
AC_CHECK_LIB(nv, nvlist_create)
AC_CHECK_LIB(casper, cap_init)
AC_CHECK_LIB(cap_sysctl, cap_sysctlbyname)

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h])
AC_CHECK_HEADERS([sys/nv.h sys/capsicum.h libcasper.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_REALLOC
AC_CHECK_FUNCS([strerror])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

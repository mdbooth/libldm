# libldm
# Copyright 2012 Red Hat Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

AC_PREREQ([2.68])
AC_INIT([libldm], [0.1], [mbooth@redhat.com])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([src/ldm.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign])

m4_ifndef([AM_SILENT_RULES], [m4_define([AM_SILENT_RULES],[])])
AM_SILENT_RULES([yes]) # make --enable-silent-rules the default.

AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CC_C99
AC_PROG_CXX
LT_INIT

# Checks for libraries.
AC_CHECK_FUNCS([bzero])
AC_FUNC_MALLOC

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h unistd.h sys/ioctl.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

PKG_CHECK_MODULES([GOBJECT], [gobject-2.0 >= 2.26.0],
    [
        AC_SUBST([GOBJECT_CFLAGS])
        AC_SUBST([GOBJECT_LIBS])
    ]
)

PKG_CHECK_MODULES([JSON], [json-glib-1.0 >= 0.14.0],
    [
        AC_SUBST([JSON_CFLAGS])
        AC_SUBST([JSON_LIBS])
    ]
)

PKG_CHECK_MODULES([GIO_UNIX], [gio-unix-2.0 >= 2.32.0],
    [
        AC_SUBST([GIO_UNIX_CFLAGS])
        AC_SUBST([GIO_UNIX_LIBS])
    ]
)

PKG_CHECK_MODULES([DEVMAPPER], [devmapper >= 1.0],
    [
        AC_SUBST([DEVMAPPER_CFLAGS])
        AC_SUBST([DEVMAPPER_LIBS])
    ]
)

GTK_DOC_CHECK([1.14])

# Checks for library functions.

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([src/Makefile])
AC_CONFIG_FILES([test/Makefile])
AC_CONFIG_FILES([docs/reference/libldm/Makefile])
AC_OUTPUT

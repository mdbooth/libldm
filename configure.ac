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
AC_INIT([libldm], [0.2.4], [mbooth@redhat.com])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([src/ldm.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign])

# Define _GNU_SOURCE etc.
AC_USE_SYSTEM_EXTENSIONS

AC_PATH_PROG([XSLTPROC], [xsltproc])

m4_ifndef([AM_SILENT_RULES], [m4_define([AM_SILENT_RULES],[])])
AM_SILENT_RULES([yes]) # make --enable-silent-rules the default.

AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CC_C99
LT_INIT

AC_CHECK_LIB([readline], [readline], [],
             [AC_MSG_ERROR([readline library is missing])])

# Checks for libraries.
AC_CHECK_FUNCS([bzero strerror])
AC_FUNC_MALLOC

# Checks for header files.
AC_CHECK_HEADERS(
    [ fcntl.h \
      stdint.h \
      stdlib.h \
      string.h \
      sys/ioctl.h \
      unistd.h ],
    [],
    [AC_MSG_ERROR([At least 1 required header file is missing])]
)
AC_HEADER_MAJOR

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

PKG_CHECK_MODULES([ZLIB], [zlib >= 1.2],
    [
        AC_SUBST([ZLIB_CFLAGS])
        AC_SUBST([ZLIB_LIBS])
    ]
)

PKG_CHECK_MODULES([UUID], [uuid],
    [
        AC_SUBST([UUID_CFLAGS])
        AC_SUBST([UUID_LIBS])
    ]
)

PKG_CHECK_MODULES([DEVMAPPER], [devmapper >= 1.02],
    [
        AC_SUBST([DEVMAPPER_CFLAGS])
        AC_SUBST([DEVMAPPER_LIBS])
    ]
)

# GObject Introspection is not working. See comment in src/Makefile.am
# GOBJECT_INTROSPECTION_CHECK([1.30.0])
GTK_DOC_CHECK([1.14], [--flavour no-tmpl])

# Checks for library functions.

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([src/Makefile])
AC_CONFIG_FILES([test/Makefile])
AC_CONFIG_FILES([docs/reference/libldm/Makefile])
AC_CONFIG_FILES([docs/reference/ldmtool/Makefile])
AC_CONFIG_FILES([ldm-1.0.pc])
AC_CONFIG_FILES([libldm.spec])
AC_OUTPUT

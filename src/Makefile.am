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

AM_CFLAGS = -Wall -Werror -Wshadow -Wextra \
  -Wno-unused-local-typedefs \
  -Wno-unused-parameter

libname = libldm-1.0.la
lib_LTLIBRARIES = $(libname)

include_HEADERS = ldm.h

libldm_1_0_la_SOURCES = mbr.h mbr.c gpt.h gpt.c ldm.h ldm.c
libldm_1_0_la_CFLAGS = $(AM_CFLAGS) $(GOBJECT_CFLAGS) $(ZLIB_CFLAGS) $(UUID_CFLAGS) $(DEVMAPPER_CFLAGS)
libldm_1_0_la_LIBADD = $(ZLIB_LIBS) $(UUID_LIBS) $(GOBJECT_LIBS) $(DEVMAPPER_LIBS)

bin_PROGRAMS = ldmtool

ldmtool_CFLAGS = $(AM_CFLAGS) $(GOBJECT_CFLAGS) $(JSON_CFLAGS) \
		 $(GIO_UNIX_CFLAGS)
ldmtool_LDADD = -lreadline $(builddir)/$(libname) $(GOBJECT_LIBS) \
		$(JSON_LIBS) $(GIO_UNIX_LIBS)

# GObject introspection fails. This seems to be because g-ir-scanner incorrectly
# guesses the symbol prefix as 'l_dm', although explicitly passing in the
# correct prefix causes it not to recognise any of the objects name symbols. I'm
# fairly sure this is going to require a fix in g-ir-scanner.

#include $(INTROSPECTION_MAKEFILE)
#
#INTROSPECTION_GIRS = LDM-1.0.gir
#INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --warn-all
#INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)
#
#introspection_sources = ldm.h ldm.c
#
#LDM-1.0.gir: $(libname)
#LDM_1_0_gir_INCLUDES = GObject-2.0
#LDM_1_0_gir_CFLAGS = $(INCLUDES) -I$(srcdir)
#LDM_1_0_gir_LIBS = $(libname)
#LDM_1_0_gir_FILES = $(patsubst %,$(srcdir)/%,$(introspection_sources))
#
#girdir = $(datadir)/gir-1.0
#gir_DATA = $(INTROSPECTION_GIRS)
#
#typelibdir = $(libdir)/girepository-1.0
#typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

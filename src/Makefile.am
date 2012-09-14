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

AM_CFLAGS = -Wall -Werror

lib_LTLIBRARIES = libldm-1.0.la

libldm_1_0_ladir = $(includedir)/libldm
libldm_1_0_la_HEADERS = ldm.h
libldm_1_0_la_SOURCES = mbr.c mbr.h gpt.c gpt.h ldm.c
libldm_1_0_la_CFLAGS = $(AM_CFLAGS) $(GOBJECT_CFLAGS) $(DEVMAPPER_CFLAGS)
libldm_1_0_la_LIBADD = -lz -luuid $(GOBJECT_LIBS) $(DEVMAPPER_LIBS)

bin_PROGRAMS = ldmtool

ldmtool_CFLAGS = $(AM_CFLAGS) $(GOBJECT_CFLAGS) $(JSON_CFLAGS) \
		 $(GIO_UNIX_CFLAGS)
ldmtool_LDADD = -lreadline $(builddir)/libldm-1.0.la $(GOBJECT_LIBS) \
		$(JSON_LIBS) $(GIO_UNIX_LIBS)

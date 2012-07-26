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

noinst_PROGRAMS = partread ldmread

partread_CFLAGS = -I$(top_srcdir)/src
partread_LDADD = $(top_builddir)/src/libldm.la

ldmread_CFLAGS = -I$(top_srcdir)/src $(GOBJECT_CFLAGS)
ldmread_LDADD = $(top_builddir)/src/libldm.la

img_files = \
    data/ldm-2003r2-mirrored-1.img \
    data/ldm-2003r2-mirrored-2.img \
    data/ldm-2003r2-raid5-1.img \
    data/ldm-2003r2-raid5-2.img \
    data/ldm-2003r2-raid5-3.img \
    data/ldm-2003r2-simple.img \
    data/ldm-2003r2-spanned-1.img \
    data/ldm-2003r2-spanned-2.img \
    data/ldm-2003r2-striped-1.img \
    data/ldm-2003r2-striped-2.img \
    data/ldm-2008r2-mirrored-1.img \
    data/ldm-2008r2-mirrored-2.img \
    data/ldm-2008r2-raid5-1.img \
    data/ldm-2008r2-raid5-2.img \
    data/ldm-2008r2-raid5-3.img \
    data/ldm-2008r2-spanned-1.img \
    data/ldm-2008r2-spanned-2.img \
    data/ldm-2008r2-striped-1.img \
    data/ldm-2008r2-striped-2.img

$(img_files): data/ldm-data.tar.xz Makefile
	tar -C $(builddir)/data -SJxf $< && touch $(img_files)

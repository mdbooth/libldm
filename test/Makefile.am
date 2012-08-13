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

check_PROGRAMS = partread ldmread ldmtables

partread_CFLAGS = -I$(top_srcdir)/src
partread_LDADD = $(top_builddir)/src/libldm.la

ldmread_CFLAGS = -I$(top_srcdir)/src $(GOBJECT_CFLAGS)
ldmread_LDADD = $(top_builddir)/src/libldm.la

ldmtables_CFLAGS = -I$(top_srcdir)/src $(GOBJECT_CFLAGS)
ldmtables_LDADD = $(top_builddir)/src/libldm.la

2003R2_SIMPLE = \
    data/ldm-2003r2-simple.img

2003R2_SPANNED = \
    data/ldm-2003r2-spanned-1.img \
    data/ldm-2003r2-spanned-2.img

2003R2_STRIPED = \
    data/ldm-2003r2-striped-1.img \
    data/ldm-2003r2-striped-2.img

2003R2_MIRRORED = \
    data/ldm-2003r2-mirrored-1.img \
    data/ldm-2003r2-mirrored-2.img
2003R2_MIRRORED_partial_1 = \
    data/ldm-2003r2-mirrored-1.img
2003R2_MIRRORED_partial_2 = \
    data/ldm-2003r2-mirrored-2.img

2003R2_RAID5 = \
    data/ldm-2003r2-raid5-1.img \
    data/ldm-2003r2-raid5-2.img \
    data/ldm-2003r2-raid5-3.img
2003R2_RAID5_partial_1 = \
    data/ldm-2003r2-raid5-1.img \
    data/ldm-2003r2-raid5-2.img
2003R2_RAID5_partial_2 = \
    data/ldm-2003r2-raid5-2.img \
    data/ldm-2003r2-raid5-3.img
2003R2_RAID5_partial_3 = \
    data/ldm-2003r2-raid5-1.img \
    data/ldm-2003r2-raid5-3.img

2008R2_SPANNED = \
    data/ldm-2008r2-spanned-1.img \
    data/ldm-2008r2-spanned-2.img

2008R2_STRIPED = \
    data/ldm-2008r2-striped-1.img \
    data/ldm-2008r2-striped-2.img

2008R2_MIRRORED = \
    data/ldm-2008r2-mirrored-1.img \
    data/ldm-2008r2-mirrored-2.img
2008R2_MIRRORED_partial_1 = \
    data/ldm-2008r2-mirrored-1.img
2008R2_MIRRORED_partial_2 = \
    data/ldm-2008r2-mirrored-2.img

2008R2_RAID5 = \
    data/ldm-2008r2-raid5-1.img \
    data/ldm-2008r2-raid5-2.img \
    data/ldm-2008r2-raid5-3.img
2008R2_RAID5_partial_1 = \
    data/ldm-2008r2-raid5-1.img \
    data/ldm-2008r2-raid5-2.img
2008R2_RAID5_partial_2 = \
    data/ldm-2008r2-raid5-2.img \
    data/ldm-2008r2-raid5-3.img
2008R2_RAID5_partial_3 = \
    data/ldm-2008r2-raid5-1.img \
    data/ldm-2008r2-raid5-3.img

img_files = \
    $(2003R2_SIMPLE) \
    $(2003R2_SPANNED) \
    $(2003R2_STRIPED) \
    $(2003R2_MIRRORED) \
    $(2003R2_RAID5) \
    $(2008R2_SPANNED) \
    $(2008R2_STRIPED) \
    $(2008R2_MIRRORED) \
    $(2008R2_RAID5)

$(img_files): data/ldm-data.tar.xz Makefile
	tar -C $(builddir)/data -SJxf $< && touch $(img_files)

data: $(img_files)

# The RAID5 partial tests aren't passing. Kernel error message is:
# md/raid:mdX: cannot start dirty degraded array.
TESTS = \
    2003R2_SIMPLE \
    2003R2_SPANNED \
    2003R2_STRIPED \
    2003R2_MIRRORED \
    2003R2_RAID5 \
    2008R2_SPANNED \
    2008R2_STRIPED \
    2008R2_MIRRORED \
    2008R2_RAID5 \
    2003R2_MIRRORED_partial_1 \
    2003R2_MIRRORED_partial_2 \
    2008R2_MIRRORED_partial_1 \
    2008R2_MIRRORED_partial_2 \
    #2003R2_RAID5_partial_1 \
    #2003R2_RAID5_partial_2 \
    #2003R2_RAID5_partial_3 \
    #2008R2_RAID5_partial_1 \
    #2008R2_RAID5_partial_2 \
    #2008R2_RAID5_partial_3

$(TESTS): Makefile.am checkmount.pl
	echo "#!/bin/sh" > $@
	echo "sudo ./checkmount.pl $($@)" >> $@
	chmod 755 $@

.PHONY: data

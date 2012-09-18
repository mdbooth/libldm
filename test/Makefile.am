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

EXTRA_DIST = checkmount.pl data/ldm-data.tar.xz

check_PROGRAMS = partread ldmread

partread_CFLAGS = $(AM_CFLAGS) -I$(top_srcdir)/src
partread_LDADD = $(top_builddir)/src/libldm-1.0.la

ldmread_CFLAGS = $(AM_CFLAGS) -I$(top_srcdir)/src $(GOBJECT_CFLAGS)
ldmread_LDADD = $(top_builddir)/src/libldm-1.0.la $(GOBJECT_LIBS)

2003R2_DG = 03c0c4fc-8b6f-402b-9431-4be2e5823b1c
2008R2_DG = 06495a84-fbfd-11e1-8cf9-52540061f5db

2003R2_SIMPLE = \
    data/ldm-2003r2-simple-1.img
2003R2_SIMPLE_volume = $(2003R2_DG) Volume1

2003R2_SPANNED = \
    data/ldm-2003r2-spanned-1.img \
    data/ldm-2003r2-spanned-2.img
2003R2_SPANNED_volume = $(2003R2_DG) Volume2

2003R2_STRIPED = \
    data/ldm-2003r2-striped-1.img \
    data/ldm-2003r2-striped-2.img
2003R2_STRIPED_volume = $(2003R2_DG) Stripe1

2003R2_MIRRORED = \
    data/ldm-2003r2-mirrored-1.img \
    data/ldm-2003r2-mirrored-2.img
2003R2_MIRRORED_partial_1 = \
    data/ldm-2003r2-mirrored-1.img
2003R2_MIRRORED_partial_2 = \
    data/ldm-2003r2-mirrored-2.img
2003R2_MIRRORED_volume = $(2003R2_DG) Volume3
2003R2_MIRRORED_partial_1_volume = $(2003R2_DG) Volume3
2003R2_MIRRORED_partial_2_volume = $(2003R2_DG) Volume3

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
2003R2_RAID5_volume = $(2003R2_DG) Raid1
2003R2_RAID5_partial_1_volume = $(2003R2_DG) Raid1
2003R2_RAID5_partial_2_volume = $(2003R2_DG) Raid1
2003R2_RAID5_partial_3_volume = $(2003R2_DG) Raid1

2008R2_SPANNED = \
    data/ldm-2008r2-spanned-1.img \
    data/ldm-2008r2-spanned-2.img
2008R2_SPANNED_volume = $(2008R2_DG) Volume1

2008R2_STRIPED = \
    data/ldm-2008r2-striped-1.img \
    data/ldm-2008r2-striped-2.img
2008R2_STRIPED_volume = $(2008R2_DG) Volume2

2008R2_MIRRORED = \
    data/ldm-2008r2-mirrored-1.img \
    data/ldm-2008r2-mirrored-2.img
2008R2_MIRRORED_partial_1 = \
    data/ldm-2008r2-mirrored-1.img
2008R2_MIRRORED_partial_2 = \
    data/ldm-2008r2-mirrored-2.img
2008R2_MIRRORED_volume = $(2008R2_DG) Volume3
2008R2_MIRRORED_partial_1_volume = $(2008R2_DG) Volume3
2008R2_MIRRORED_partial_2_volume = $(2008R2_DG) Volume3

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
2008R2_RAID5_volume = $(2008R2_DG) Volume4
2008R2_RAID5_partial_1_volume = $(2008R2_DG) Volume4
2008R2_RAID5_partial_2_volume = $(2008R2_DG) Volume4
2008R2_RAID5_partial_3_volume = $(2008R2_DG) Volume4

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
	mkdir -p $(builddir)/data
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

$(TESTS): Makefile.am checkmount.pl $(img_files)
	echo "#!/bin/sh" > $@
	echo "sudo $(srcdir)/checkmount.pl $(top_builddir)/src $($@_volume) $($@)" >> $@
	chmod 755 $@

.PHONY: data

CLEANFILES = $(TESTS) $(img_files)

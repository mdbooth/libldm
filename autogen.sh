#!/bin/bash

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

set -e
set -v

# Create the m4 include directory
mkdir -p m4

# Initialise gtk-doc
gtkdocize

# Initialize autotools
autoreconf -i

CONFIGUREDIR=.

# Run configure in BUILDDIR if it's set
if [ ! -z "$BUILDDIR" ]; then
    mkdir -p $BUILDDIR
    cd $BUILDDIR

    CONFIGUREDIR=..
fi

# If no arguments were specified and configure has run before, use the previous
# arguments
if test $# == 0 && test -x ./config.status; then
    ./config.status --recheck
else
    $CONFIGUREDIR/configure "$@"
fi

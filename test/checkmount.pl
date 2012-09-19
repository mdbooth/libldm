#!/usr/bin/perl

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

use strict;
use warnings;

use File::Temp qw(tempdir);
use JSON::PP;

my @loops;
my @dm_devices;

my $mount = tempdir(CLEANUP => 1);
my $mounted = 0;

my $builddir = shift @ARGV;
my $dg = shift @ARGV;
my $vol = shift @ARGV;

sub umount
{
    my $dir = shift;

    my $tries = 0;
    do {
        return if (system('umount', $mount) == 0);
        sleep(1);
        $tries++;
    } while ($tries < 3)
}

END {
    my $err = $?;

    umount($mount) if (defined($mount) && $mounted);

    while(@dm_devices) {
        system('dmsetup', 'remove', pop(@dm_devices));
    }

    while(@loops) {
        system('losetup', '-d', pop(@loops));
    }

    $? = $err;
}

my @loop_args;
foreach my $arg (@ARGV) {
    my $loop = `losetup -f --show $arg`;
    chomp($loop);
    push(@loops, $loop);
    push(@loop_args, '-d', $loop);
}

my $json = JSON::PP->new();
open(JSON, '-|', "$builddir/ldmtool", @loop_args, 'create', 'volume', $dg, $vol)
    or die("Unable to run ldmtool");
while(<JSON>) {
    $json->incr_parse($_);
}
close(JSON);

my $tested = 0;
foreach my $device (@{$json->incr_parse}) {
    system('mount', "/dev/mapper/$device", $mount);
    $mounted = 1;

    open(TEST, '<', "$mount/test.txt") or die "Test file missing";
    my $line = <TEST>;
    close(TEST);
    umount($mount);
    $mounted = 0;

    `$builddir/ldmtool remove all`;

    exit(1) unless $line eq 'Filesystem test';
    $tested = 1;
}

exit($tested ? 0 : 1);

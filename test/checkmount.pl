#!/usr/bin/perl

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

END {
    my $err = $?;

    system('umount', $mount) if (defined($mount) && $mounted);

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
    system('umount', $mount);
    $mounted = 0;

    `$builddir/ldmtool remove all`;

    exit(1) unless $line eq 'Filesystem test';
    $tested = 1;
}

exit($tested ? 0 : 1);

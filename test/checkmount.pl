#!/usr/bin/perl

use strict;
use warnings;

use File::Temp qw(tempdir);

my @loops;
my @dm_devices;

my $mount = tempdir(CLEANUP => 1);

END {
    my $err = $?;

    system('umount', $mount) if defined($mount);

    while(@dm_devices) {
        system('dmsetup', 'remove', pop(@dm_devices));
    }

    while(@loops) {
        system('losetup', '-d', pop(@loops));
    }

    $? = $err;
}

foreach my $arg (@ARGV) {
    my $loop = `losetup -f --show $arg`;
    chomp($loop);
    push(@loops, $loop);
}

open(LDMTABLES, '-|', './ldmtables', @loops) or die("Unable to run ldmtables");
while(<LDMTABLES>) {
    chomp;
    my $device = $_;

    push(@dm_devices, $device);
    open(DMSETUP, '|-', 'dmsetup', 'create', $device);
    while(<LDMTABLES>) {
        chomp;
        last if $_ eq "";

        print DMSETUP $_,"\n";
    }
    close(DMSETUP);
}
close(LDMTABLES);

system('mount', '/dev/mapper/'.$dm_devices[$#dm_devices], $mount);

my $opened;
foreach my $file ('test.txt', 'test.txt.txt') {
    open(TEST, '<', "$mount/$file") or next;
    $opened = 1;
    last;
}
die "Test file missing" unless defined($opened);

my $line = <TEST>;
close(TEST);

exit(0) if $line eq 'Filesystem test';
exit(1);

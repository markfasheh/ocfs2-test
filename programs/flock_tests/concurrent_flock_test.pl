#!/usr/bin/perl
#
# concurrent_flock_test.pl - A handy tool to test concurrent flock() on single
#                            node
#
# Initialized by Sunil Mushran <sunil.mushran@oracle.com>
# Modified by Coly Li <coly.li@suse.de>
#
# Usage: concurrent_flock_test.pl <dir> <interation> <fork_number>
#
# Example: concurrent_flock_test.pl /mnt/test/ 5000 20
#
# /mnt/test is directory within mounted ocfs2 volume, the above example
# encloses the 20 fork loop into another loop that changes the filename.
# To startwith, touch 5000 files(test.flock%d). umount and mount again.
# This will drop the caches. Now before running any ls, run flock.pl.It
# will spawn 20 processes to flock() each file and wait till the 20 forks
# have exited before letting loose another 20 for the next file. (Makesure
# you have another node or two or more that have just mounted that volume.).
#


use warnings;
use Fcntl ':flock';

my $dir=$ARGV[0];
my $interation=$ARGV[1];
my $fork_nr=$ARGV[2];

$| = 1;
if (! ($dir && $interation && $fork_nr)) {
	print "Usage: concurrent_flock_test.pl <dir> <interation> <fork_number>\n";
	exit(1);
}
if (! -d $dir) {
	print "$dir is not directory.\n";
	exit(1);
}

system("rm -f $dir/test.lock*");
for (my $y = 0; $y < $interation; $y ++) {
	my $file = $dir . "/test.lock" . $y;

	printf "[%s] %s\n", $$, $file;

	for (my $x = 0; $x < $fork_nr; $x ++) {
		my $pid = fork();
		if ($pid == 0) {
			my $fh;
			open($fh, ">> $file") or die("Can't open '$file': $!");
			printf "[%s] lock %s: %s\n", $$, $file,
			flock($fh, LOCK_EX) ? 'done' : $!;
			close($fh);
			exit(0);
		}
	}
	while (wait > 0) { ; }
}

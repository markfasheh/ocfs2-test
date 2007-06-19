README-enospc.txt


Introduction:
=============


The enospc_test program was created as a way to quickly reproduce the problem 
reported in bugzilla#863 (http://oss.oracle.com/bugzilla/show_bug.cgi?id=863). 




Original Problem:
=================


Customer reported that filling the archive filesystem to 100% and then deleting some of the files to make room, caused a panic.




Reproducing the problem:
========================

To reproduce the problem, run the enospc.sh script as root. The syntax is:

enospc.sh <outdir> <DEVICE>

Where:

outdir - A directory where the logfile will be created.
DEVICE - A partition with at least 1GB.

The problem should reproduce on the first attempt. If the problem does not
reproduce on the first attempt, it would be better to get it executed at least
another 50 times to make sure the problem is fixed.

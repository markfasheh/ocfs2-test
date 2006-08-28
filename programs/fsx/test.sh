
. $CT_FUNCTIONS

PATH="$PATH:$CT_TST"

if ! which fsx > /dev/null 2>&1; then
	invalid "couldn't find fsx binary in my path"
fi

args="-S $RANDOM -R -W -N 1000 -p 200 $CT_DIR/fsxfile"
out "first regular read/write file io ($args)"
fsx $args || fail "fsx returned $?"

args="-A $args"
out "this time including aio ($args)"
fsx $args || fail "fsx returned $?"

# can't do this yet because O_DIRECT extending doesn't zero in the
# in the sparse regions created.  fsx will read them and get garbage 
# and complain.
#
#pagesize=$(getconf PAGE_SIZE) || invalid "getconf PAGE_SIZE failed"
#
#args="-Z -r $pagesize -w $pagesize $args"
#out "this time including page-sized DIO ($args)"
#fsx $args || fail "fsx returned $?"

pass "all combinations exited ok" 

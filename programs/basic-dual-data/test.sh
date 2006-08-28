
. $CT_FUNCTIONS
needs_remotes 1

echo "local" > $CT_DIR/writeread || fail "local write failed"

contents=$(remote 0 cat $CT_REMOTE_DIR_0/writeread) || 
	fail "couldn't get remote writeread contents"

if [ "$contents" != "local" ]; then
	fail "remote '$contents' != 'local'"
fi

remote 0 "echo remote > $CT_REMOTE_DIR_0/writeread" || fail "remote write failed"

contents=$(cat $CT_DIR/writeread) || 
	fail "couldn't get local writeread contents"

if [ "$contents" != "remote" ]; then
	fail "local '$contents' != 'remote'"
fi


ddstring="thequickbrownfoxjumpsoverthelazydog"

doit_0=""
doit_1="remote 0"

whichnode=""
whichpath="$CT_DIR"

for c in $(seq 0 $((${#ddstring} - 1))); do
	echo $ddstring | \
		$whichnode dd conv=notrunc of=$whichpath/bytedd \
			count=1 bs=1 skip=$c seek=$c 2>/dev/null ||
			fail "$whichnode dd failed on byte $c"

	if [ -z "$whichnode" ]; then
		whichnode="remote 0"
		whichpath="$CT_REMOTE_DIR_0"
	else
		whichnode=""
		whichpath="$CT_DIR"
	fi
done

contents=$(cat $CT_DIR/bytedd) || 
	fail "couldn't get local bytedd contents"

if [ "$contents" != "$ddstring" ]; then
	fail "local '$contents' != '$ddstring'"
fi

contents=$(remote 0 cat $CT_REMOTE_DIR_0/bytedd) || 
	fail "couldn't get remote bytedd contents"

if [ "$contents" != "$ddstring" ]; then
	fail "remote '$contents' != '$ddstring'"
fi

pass contents ok

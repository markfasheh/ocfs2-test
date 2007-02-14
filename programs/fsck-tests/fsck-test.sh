#!/bin/bash
#
# Copyright (C) 2006 Oracle.  All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License, version 2,  as published by the Free Software Foundation.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
# 
# You should have received a copy of the GNU General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 021110-1307, USA.
#

################################################################

#
# warn_if_bad		Put out warning message(s) if $1 has bad RC.
#
#	$1	0 (pass) or non-zero (fail).
#	$2+	Remaining arguments printed only if the $1 is non-zero.
#
#	Incoming $1 is returned unless it is 0
#
function warn_if_bad()
{
	local -i rc="$1"
	local script="${0##*/}"

	# Ignore if no problems
	[ "$rc" -eq "0" ] && return 0

	# Broken
	shift
	echo "$script: $@" >&2
	return "$rc"
}

#
# exit_if_bad		Put out error message(s) if $1 has bad RC.
#
#	$1	0 (pass) or non-zero (fail).
#	$2+	Remaining arguments printed only if the $1 is non-zero.
#
#               Exits with 1 unless $1 is 0
#
function exit_if_bad()
{
	warn_if_bad "$@" || exit 1
	return 0
}

#
# tc_root_or_break
#
#	Breaks the testcase if not running as root.
#
#	If this returns 1, the invoker MUST abort the testcase.
#
#	returns 0 if running as root
#	returns 1 if not (and breaks testcase)
#
function check_root_or_exit()
{
	[ "$UID" -eq "0" ]
	exit_if_bad "$?" "Must be run by UID=0. Actual UID=$UID."
	return 0
}

#
# check_executes	Check for executable(s)
#
#	Returns 0 if true.
#	Returns 1 if not.
#
function check_executes()
{
	local cmd
	local all_ok="yes"
	for cmd in "$@"
	do
		if ! type "$cmd" &>/dev/null
		then
			echo "Command \"$cmd\" not found" >&2
			all_ok="no"
		fi
	done
	[ "$all_ok" = "yes" ]
}

#
# check_exec_or_exit	Check for required executables.
#
#	Exits (not returns) if commands listed on command line do not exist.
#
#	Returns 0 if true.
#	Exits with 1 if not.
#
function check_exec_or_exit()
{
	check_executes "$@"
	exit_if_bad "$?" "Above listed required command(s) not found"
	return 0
}

################################################################

#
# internal_setup	Script setup
#
#	Create a temporary directory and put the path of the temporary
#	directory into shell variable TMP_DIR.
#
#	Returns 0 on success.
#	Exits (not returns) with 1 on failure.
#
function internal_setup()
{
	# Trap exit for internal_cleanup function.
	trap "internal_cleanup" 0

	umask 0077

	BASE_DIR="${0%/*}"
	TMP_DIR="/tmp/${0##*/}-$$"
	mkdir -p "$TMP_DIR"

	check_exec_or_exit cat mktemp wc yes
}

#
# internal_cleanup	Script cleanup (reached via trap 0)
#
#	Destory any temporarily facility created by internal_setup.
#
function internal_cleanup()
{
	test_summary

	if [ "$TMP_DIR" ]
	then
		rm -rf "$TMP_DIR"
		unset TMP_DIR
	fi
}

#
# usage			Display help information and exit.
#
function usage()
{
	local script="${0##*/}"
	cat <<-EOF
	Usage: $script [options] device

	Options:
	      --help                        display this help and exit
	      --disk-size={small|medium}    select the disk size for testing
	                      \`\`small''  => 1K/1K block/cluster size for 256M partition
	                      \`\`medium'' => 1K/4K block/cluster size for 40G  partition
	      --with-logdir=DIR                 save testing log to DIR
	      --with-fsck=PROGRAM           use the PROGRAM as fsck.ocfs2
	      --with-fswreck=PROGRAM        use the PROGRAM as fswreck
	      --with-mkfs=PROGRAM           use the PROGRAM as mkfs.ocfs2
	      --with-corrupt=CORRUPTS       use specified fswreck corrupt code

	Examples:

	  $script --with-fswreck=../fswreck/fswreck /dev/sde2
	  $script --with-corrupt="06 22 36" /dev/sde2
	EOF
}

################################################################

BASE_DIR=""
TMP_DIR=""
DEVICE=""
MKFS_BIN=""
FSCK_BIN=""
FSWRECK_BIN=""
declare -i NUM_OF_TESTS=0
declare -i NUM_OF_PASS=0
declare -i NUM_OF_FAIL=0
declare -i NUM_OF_BROKEN=0
CORRUPT=""
DISK_SIZE="small"
LOG_DIR=""

#
# ext_setup		Guess the position of fsck.ocfs2, fswreck and fill
#			FSCK_BIN, FSWRECK_BIN
#
function ext_setup()
{
	[ -z "$FSCK_BIN" ] && FSCK_BIN="$(which fsck.ocfs2 2>/dev/null)"
	[ -x "$FSCK_BIN" ]
	exit_if_bad "$?" "Command \"fsck.ocfs2\" not found"
	[ -z "$FSWRECK_BIN" ] && FSWRECK_BIN="$(which fswreck 2>/dev/null)"
	[ -x "$FSWRECK_BIN" ]
	exit_if_bad "$?" "Command \"fswreck\" not found"
	[ -z "$MKFS_BIN" ] && MKFS_BIN="$(which mkfs.ocfs2 2>/dev/null)"
	[ -x "$MKFS_BIN" ]
	exit_if_bad "$?" "Command \"mkfs.ocfs2\" not found"
}

CURRENT_TEST=""
STDOUT="/dev/null"
STDERR="/dev/null"

#
#	$1	Testcase name
#
function test_setup()
{
	(( ++NUM_OF_TESTS ))
	[ "$CURRENT_TEST" ] && test_broken
	CURRENT_TEST="$1"
	STDOUT=$(mktemp -p "$TMP_DIR")
	STDERR=$(mktemp -p "$TMP_DIR")
	test_info "starting"
}

#
#	$1	The testcase output information
#
function test_info()
{
	echo -n "$CURRENT_TEST"
	echo -n " "
	local -i i=${#CURRENT_TEST}
	while (( i < 20 ))
	do
		echo -n "."
		(( ++i ))
	done
	echo -n " "
	echo "$@"
}

#
# test_pass	Testcase pass
#
function test_pass()
{
	(( ++NUM_OF_PASS ))
	test_info "PASS"
	CURRENT_TEST=""
}

#
# test_fail	Testcase fail and print out the stdout and stderr
#
function test_fail()
{
	(( ++NUM_OF_FAIL ))
	local info="$@"
	[ "$info" ] && test_info "$info"
	test_info "FAIL"
	echo "=== The following is the stdout ==="
	cat "$STDOUT" 2>/dev/null
	echo "=== The above is the stdout ==="
	echo "=== The following is the stderr ==="
	cat "$STDERR" 2>/dev/null
	echo "=== The above is the stderr ==="
	CURRENT_TEST=""
}

#
# test_broken	Testcase broken and print out the stdout and stderr
#
function test_broken()
{
	(( ++NUM_OF_BROKEN ))
	local info="$@"
	[ "$info" ] && test_info "$info"
	test_info "BROKEN"
	echo "=== The following is the stdout ==="
	cat "$STDOUT" 2>/dev/null
	echo "=== The above is the stdout ==="
	echo "=== The following is the stderr ==="
	cat "$STDERR" 2>/dev/null
	echo "=== The above is the stderr ==="
	CURRENT_TEST=""
}

#
#	$1	0 (pass) or non-zero (fail).
#	$2+	Remaining arguments printed only if the $1 is non-zero.
#
function test_fail_if_bad()
{
	local -i rc="$1"
	shift
	local info="$@"
	# Ignore if no problems
	[ "$rc" -eq "0" -a "$(wc -c <$STDERR)" -eq "0" ] && return 0
	test_fail "$info"
	return 1
}

#
#	$1	0 (pass) or non-zero (fail).
#	$2+	Remaining arguments printed only if the $1 is non-zero.
#
function test_pass_or_fail()
{
	local -i rc="$1"
	shift
	local info="$@"
	if [ "$rc" -eq "0" -a "$(wc -c <$STDERR)" -eq "0" ]
	then
		test_pass
	else
		test_fail "$info"
	fi
}

function test_summary()
{
	[ 0 -eq "$NUM_OF_TESTS" ] && return
	[ "$CURRENT_TEST" ] && test_broken

	cat <<-EOF
	=============================================================================
	Test Summary
	------------------------------------
	Number of tests:	$NUM_OF_TESTS
	Number of passed tests:	$NUM_OF_PASS
	Number of failed tests:	$NUM_OF_FAIL
	Number of broken tests: $NUM_OF_BROKEN
	=============================================================================
	EOF

	[ "$LOG_DIR" ] && mkdir -p "$LOG_DIR" && cp -a "$TMP_DIR"/fsck.* "$LOG_DIR"
}

function smoke_test()
{
	test_setup "smoke test"
	"$FSCK_BIN" -V >"$STDOUT" 2>"$STDERR"
	test_pass_or_fail 0
}

#	$2	Disk size, should be ``small'', ``medium'' or ``large''.
function corrupt_test()
{
	local corrupt="$1"
	local disk_size="$2"

	# Small disk
	local -a mkfs_profile_small=(
		1024		# block size
		4096		# cluster size
		4		# number of node solts
		"4M"		# journal
		262144		# blocks count
	);
	# Medium disk
	local -a mkfs_profile_medium=(
		4096		# block size
		4096		# cluster size
		4		# number of node solts
		"16M"		# journal
		1048576		# blocks count
	);
	# Large disk
	local -a mkfs_profile_large=(
		4096		# block size
		131072		# cluster size
		4		# number of node solts
		"64M"		# journal
		16777216	# blocks count
	);

	local v="mkfs_profile_${disk_size}[@]"
	local -a mkfs_profile=("${!v}")

	test_setup "corrupt test $corrupt"

	if [ 0 -eq "${#mkfs_profile[@]}" ]
	then
		test_broken "Unknown disk size"
		return
	fi

	dd if=/dev/zero of=$DEVICE bs=1M count=4 &>"$STDOUT"
	test_info "dd if=/dev/zero of=$DEVICE bs=1M count=4"
	test_fail_if_bad "$?" "dd failed" || return

	test_info mkfs.ocfs2 -b "${mkfs_profile[0]}" -C "${mkfs_profile[1]}" \
		-N "${mkfs_profile[2]}" -J "size=${mkfs_profile[3]}" \
		"$DEVICE" "${mkfs_profile[4]}"
	yes | "$MKFS_BIN" -b "${mkfs_profile[0]}" -C "${mkfs_profile[1]}" \
		-N "${mkfs_profile[2]}" -J "size=${mkfs_profile[3]}" \
		"$DEVICE" "${mkfs_profile[4]}" &>"$STDOUT"
	test_fail_if_bad "$?" "mkfs failed" || return

	test_info "fswreck -c $corrupt $DEVICE"
	"$FSWRECK_BIN" -n 2 -c "$corrupt" "$DEVICE" &>"$STDOUT"
	test_fail_if_bad "$?" "fswreck failed" || return

	test_info "fsck.ocfs2 -fy $DEVICE"
	"$FSCK_BIN" -fy "$DEVICE" >"$STDOUT" 2>"$STDERR"
	test_fail_if_bad "$?" "fsck failed" || return

	cp "$STDOUT" "$TMP_DIR/fsck.ocfs2.$corrupt.actual.stdout" &>/dev/null
	sed -e "s#@DEVICE@#$DEVICE#" \
		<"$BASE_DIR/${disk_size}-disk/fsck.ocfs2.$corrupt.stdout" \
		>"$TMP_DIR/fsck.ocfs2.$corrupt.expect.stdout" 2>/dev/null

	diff -u -I "  uuid:              \( [0-9a-f][0-9a-f]\)\{16\}" \
		-I "\[DIRENT_INODE_FREE\] Directory entry 'test[0-9A-Za-z]\{6\}' refers to inode number [0-9]\+ which isn't allocated, clear the entry? y" \
		-I "\[EB_GEN\] An extent block at [0-9]\+ in inode [0-9]\+ has a generation of 1234 which doesn't match the volume's generation of [0-9a-f]\{8}.  Consider this extent block invalid? y" \
		"$TMP_DIR/fsck.ocfs2.$corrupt.expect.stdout" \
		"$TMP_DIR/fsck.ocfs2.$corrupt.actual.stdout" \
		>"$STDOUT" &>"$STDOUT"
	test_fail_if_bad "$?" "fsck output differ with the expect one" || return

	test_info "verifying"
	"$FSCK_BIN" -fy "$DEVICE" >"$STDOUT" 2>"$STDERR"
	test_fail_if_bad "$?" "fsck failed" || return

	cp "$STDOUT" "$TMP_DIR/fsck.ocfs2.$corrupt.actual.stdout" &>/dev/null
	[ -f "$TMP_DIR/fsck.ocfs2.clean.expect.stdout" ] ||
		sed -e "s#@DEVICE@#$DEVICE#" \
		<"$BASE_DIR/${disk_size}-disk/fsck.ocfs2.clean.stdout" \
		>"$TMP_DIR/fsck.ocfs2.clean.expect.stdout" 2>/dev/null

	diff -u -I "  uuid:              \( [0-9a-f][0-9a-f]\)\{16\}" \
		"$TMP_DIR/fsck.ocfs2.clean.expect.stdout" \
		"$TMP_DIR/fsck.ocfs2.$corrupt.actual.stdout" \
		>"$STDOUT" &>"$STDOUT"
	test_fail_if_bad "$?" "fsck output differ with the expect one" || return

	test_pass
}

function basic_test()
{
	local corrupt

	for corrupt in $CORRUPT
	do
		corrupt_test "$corrupt" "$DISK_SIZE"
	done
}

################################################################

#
# main
#
internal_setup

if [ "$#" -eq "0" ]
then
	usage
	exit 255
fi

while [ "$#" -gt "0" ]
do
	case "$1" in
	"--help")
		usage
		exit 255
		;;
	"--disk-size="*)
		DISK_SIZE="${1#--disk-size=}"
		;;
	"--with-logdir="*)
		LOG_DIR="${1#--with-logdir=}"
		;;
	"--with-fsck="*)
		FSCK_BIN="${1#--with-fsck=}"
		;;
	"--with-fswreck="*)
		FSWRECK_BIN="${1#--with-fswreck=}"
		;;
	"--with-mkfs="*)
		MKFS_BIN="${1#--with-mkfs=}"
		;;
	"--with-corrupt="*)
		CORRUPT="${1#--with-corrupt=}"
		;;
	*)
		DEVICE="$1"
		;;
	esac
	shift
done

if [ "" = "$CORRUPT" ]
then
	CORRUPT="$(seq -f "%02g" 00 43)"
fi

[ -b "$DEVICE" ]
exit_if_bad "$?" "invalid block device - $DEVICE"

check_root_or_exit
ext_setup
smoke_test
basic_test
#corrupt_test 22
#corrupt_test 36

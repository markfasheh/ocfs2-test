#!/bin/sh

set -e

PROJECT=ocfs2-test

rm -rf autom4te.cache
autoconf
./configure "$@"

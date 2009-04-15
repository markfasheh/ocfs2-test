AC_DEFUN([OCFS2_CHECK_HEADERS],
  [AC_MSG_CHECKING([for $1])
   kernel_check_regexp="m4_default([$5], [\<$1(])"

   kernel_check_headers=
   for kfile in $2; do
     kernel_check_headers="$kernel_check_headers /usr/include/$kfile"
   done

   if grep "$kernel_check_regexp" $kernel_check_headers >/dev/null 2>&1 ; then
     m4_default([$3], :)
     AC_MSG_RESULT(yes)
   else
     m4_default([$4], :)
     AC_MSG_RESULT(no)
   fi
])# OCFS2_CHECK_HEADERS

dnl $Id$
dnl config.m4 for extension hm_qlimiter

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

PHP_ARG_WITH(hm_qlimiter, for hm_qlimiter support,
dnl Make sure that the comment is aligned:
[  --with-hm_qlimiter             Include hm_qlimiter support])

dnl Otherwise use enable:

PHP_ARG_ENABLE(hm_qlimiter, whether to enable hm_qlimiter support,
dnl Make sure that the comment is aligned:
[  --enable-hm_qlimiter           Enable hm_qlimiter support])

if test "$PHP_HM_QLIMITER" != "no"; then
  dnl Write more examples of tests here...

  dnl # --with-hm_qlimiter -> check with-path
  dnl SEARCH_PATH="/usr/local /usr"     # you might want to change this
  dnl SEARCH_FOR="/include/hm_qlimiter.h"  # you most likely want to change this
  dnl if test -r $PHP_HM_QLIMITER/$SEARCH_FOR; then # path given as parameter
  dnl   HM_QLIMITER_DIR=$PHP_HM_QLIMITER
  dnl else # search default path list
  dnl   AC_MSG_CHECKING([for hm_qlimiter files in default path])
  dnl   for i in $SEARCH_PATH ; do
  dnl     if test -r $i/$SEARCH_FOR; then
  dnl       HM_QLIMITER_DIR=$i
  dnl       AC_MSG_RESULT(found in $i)
  dnl     fi
  dnl   done
  dnl fi
  dnl
  dnl if test -z "$HM_QLIMITER_DIR"; then
  dnl   AC_MSG_RESULT([not found])
  dnl   AC_MSG_ERROR([Please reinstall the hm_qlimiter distribution])
  dnl fi

  dnl # --with-hm_qlimiter -> add include path
  dnl PHP_ADD_INCLUDE($HM_QLIMITER_DIR/include)

  dnl # --with-hm_qlimiter -> check for lib and symbol presence
  dnl LIBNAME=hm_qlimiter # you may want to change this
  dnl LIBSYMBOL=hm_qlimiter # you most likely want to change this 

  dnl PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  dnl [
  dnl   PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $HM_QLIMITER_DIR/lib, HM_QLIMITER_SHARED_LIBADD)
  dnl   AC_DEFINE(HAVE_HM_QLIMITERLIB,1,[ ])
  dnl ],[
  dnl   AC_MSG_ERROR([wrong hm_qlimiter lib version or lib not found])
  dnl ],[
  dnl   -L$HM_QLIMITER_DIR/lib -lm
  dnl ])
  dnl
  dnl PHP_SUBST(HM_QLIMITER_SHARED_LIBADD)

  PHP_NEW_EXTENSION(hm_qlimiter, hm_qlimiter.c qlimiter_mmap.c, $ext_shared)
fi

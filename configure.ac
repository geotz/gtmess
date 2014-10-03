dnl    gtmess - MSN Messenger client
dnl    Copyright (C) 2002-2003  George M. Tzoumas
dnl
dnl    This program is free software; you can redistribute it and/or modify
dnl    it under the terms of the GNU General Public License as published by
dnl    the Free Software Foundation; either version 2 of the License, or
dnl    (at your option) any later version.
dnl
dnl    This program is distributed in the hope that it will be useful,
dnl    but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl    GNU General Public License for more details.
dnl
dnl    You should have received a copy of the GNU General Public License
dnl    along with this program; if not, write to the Free Software
dnl    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
dnl    This file must be processed by automake

dnl configure.ac file for gtmess

dnl Process this file with autoconf to produce a configure script.



AC_INIT
PACKAGE="gtmess"
VERSION="0.8.2"
VDATE="15 Nov 2003"
AM_INIT_AUTOMAKE($PACKAGE, $VERSION)
AM_CONFIG_HEADER(src/config.h)
AC_CONFIG_SRCDIR(src/client/gtmess.c)
AC_DEFINE_UNQUOTED(VDATE, "$VDATE", [version date])


dnl Checks for programs.
AC_PROG_CC
AC_ISC_POSIX
AC_PROG_MAKE_SET
AC_PROG_RANLIB

dnl Checks for libraries.

dnl Checks for header files.
dnl AM_C_PROTOTYPES

AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h errno.h fcntl.h netdb.h netinet/in.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM

AC_ARG_ENABLE(home, 
              [AC_HELP_STRING([--enable-home], [read run-time data from ~/.gtmess (no)])],
              [if eval "test x$enable_home = xyes"; then
                AC_DEFINE_UNQUOTED(HOME, [ ], [read run-time data from ~/.gtmess])
                AC_MSG_NOTICE([runtime data will be read from ~/.gtmess])
              fi],
              [AC_MSG_NOTICE([runtime data will be read from ${datadir}/gtmess])])

AC_ARG_ENABLE(msnp7, 
              [AC_HELP_STRING([--enable-msnp7], [enable old MSNP7 protocol (no)])],
              [if eval "test x$enable_msnp7 = xyes"; then
                AC_DEFINE_UNQUOTED(MSNP7, [ ], [enable old MSNP7 protocol])
                AC_MSG_NOTICE([old protocol MSNP7 enabled])
              fi])


AC_ARG_WITH(ssl,
            [AC_HELP_STRING([--with-ssl], [non-standard path for openssl (/usr/local/ssl)])],
            ac_cv_use_ssl=$withval, ac_cv_use_ssl=/usr/local/ssl)
if test x$ac_cv_use_ssl = xyes; then
    ac_cv_use_ssl=/usr/local/ssl
fi


dnl Checks for library functions.
AC_FUNC_FORK
dnl AC_FUNC_MALLOC
AC_TYPE_SIGNAL
dnl AC_FUNC_STAT
dnl AC_FUNC_VPRINTF
AC_CHECK_FUNCS([gettimeofday localtime_r memmove memset mkdir mkfifo select strchr strdup strerror strrchr strstr], ,
               [AC_MSG_ERROR([required function is missing])])

AC_CHECK_FUNCS([socket], ,
               [AC_CHECK_LIB(socket, socket, ,
                            [AC_MSG_ERROR([cannot find socket() function])])])
AC_CHECK_FUNCS([gethostbyname], ,
               [AC_CHECK_LIB(nsl, gethostbyname, ,
                            [AC_MSG_ERROR([cannot find gethostbyname() function])])])
                            
AC_CHECK_LIB(pthread, pthread_create, ,
             [AC_MSG_ERROR([cannot find posix threads library])])
             
AC_CHECK_LIB(ncurses, initscr, ,
               [AC_CHECK_LIB(curses, initscr, ,
                            [AC_MSG_ERROR([cannot find curses library])])])

if eval "test x$enable_msnp7 = xyes"; then
    :;
else
AC_CHECK_LIB(ssl, SSL_library_init, ,
              [
                AC_MSG_CHECKING([if -lssl exists in alternate location]);
                if test -f $ac_cv_use_ssl/lib/libssl.a; then
                    AC_MSG_RESULT(yes)
                    LDFLAGS="$LDFLAGS -L$ac_cv_use_ssl/lib"
                    CFLAGS="$CFLAGS -I$ac_cv_use_ssl/include"
                    LIBS="$LIBS -lssl -lcrypto"
                else
                    AC_MSG_RESULT(no) 
                    AC_MSG_ERROR([cannot find ssl library])
                fi
              ], [-lcrypto])
fi

AC_MSG_CHECKING([for Tcl/Tk interpreter])
WISHPROG=`which wish 2>/dev/null`
if test x$WISHPROG = x; then
    for i in `whereis wish`; do
        if test -x $i; then
            WISHPROG=$i
            break
        fi
    done
fi
if test x$WISHPROG = x; then
    AC_MSG_RESULT(not found)
    AC_MSG_WARN(edit gtmess-notify script by hand to set the interpreter)
else
    AC_MSG_RESULT($WISHPROG)
fi

dnl The following variables should be substituted in *.in

AC_SUBST(WISHPROG)
if test x$enable_home = xyes; then
    TCLDATADIR='$env(HOME)/.gtmess'
else
    eval TCLDATADIR=${datadir}/gtmess
fi
AC_SUBST(TCLDATADIR)

dnl AM_CONDITIONAL(OS_IRIX, test x`uname` = xIRIX64)

AC_CONFIG_FILES(Makefile src/Makefile src/inty/Makefile src/client/Makefile data/Makefile)
AC_CONFIG_FILES(src/gtmess-notify, chmod a+x src/gtmess-notify)

AC_OUTPUT

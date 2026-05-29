AC_INIT([aresq], [0.1], [], [aresq])
AC_CONFIG_SRCDIR([libaresq/Aresq.h])
AC_CONFIG_HEADERS([config.h])

# Auto-detect MinGW cross-compiler on Linux if --host not specified
AS_IF([test "x$host_alias" = "x"],
  [AC_CHECK_PROGS([MINGW_CXX], [i686-w64-mingw32-g++], [no])
   AS_IF([test "x$MINGW_CXX" != "xno"],
     [host_alias=$(echo "$MINGW_CXX" | sed 's/-g++$//')
      CXX=$MINGW_CXX
      CC=$(echo "$MINGW_CXX" | sed 's/-g++$/-gcc/')
      AR=${host_alias}-ar
      RANLIB=${host_alias}-ranlib
      cross_compiling=yes
      AC_MSG_NOTICE([Auto-detected MinGW cross-compiler: $host_alias])],
     [AC_MSG_NOTICE([No MinGW cross-compiler found, using native])])])

AM_INIT_AUTOMAKE([foreign -Wall])
AC_CANONICAL_HOST
AC_PROG_CXX
AM_PROG_AR
AC_PROG_RANLIB
AM_PROG_CC_C_O

# Check for headers needed by libsmb2
AC_CHECK_HEADERS([fcntl.h sys/fcntl.h stdlib.h string.h unistd.h time.h])
AC_CHECK_FUNCS([calloc free malloc time random srandom getlogin_r uselocale newlocale])

AS_IF([test "x$ac_cv_func_random" != "xyes"],
    [AC_DEFINE([NEED_RANDOM], [1], [Whether we need local random definition])])
AS_IF([test "x$ac_cv_func_srandom" != "xyes"],
    [AC_DEFINE([NEED_SRANDOM], [1], [Whether we need local srandom definition])])
AS_IF([test "x$ac_cv_func_getlogin_r" != "xyes"],
    [AC_DEFINE([NEED_GETLOGIN_R], [1], [Whether we need local getlogin_r definition])])
AC_CHECK_MEMBER([struct linger.l_linger], [
    AC_DEFINE([HAVE_LINGER], [1], [Whether we have linger])
], [], [
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif
])

# Find windres and strip with cross-compile prefix if needed
AS_IF([test "x$host_alias" != "x"],
  [AC_CHECK_PROGS([WINDRES], [${host_alias}-windres windres], [no])
   AC_CHECK_PROGS([STRIP],   [${host_alias}-strip   strip],   [strip])],
  [AC_CHECK_TOOL([WINDRES], [windres], [:])
   AC_CHECK_TOOL([STRIP],   [strip],   [strip])])

# Win Vista compatibility + Unicode
CPPFLAGS="$CPPFLAGS -DWINVER=0x0600 -D_WIN32_WINNT=0x0600"
CPPFLAGS="$CPPFLAGS -DUNICODE -D_UNICODE"

# On Windows (MinGW) asio requires Winsock2 and Mswsock libraries.
WINDOWS_SOCKET_LIBS=""
case "$host" in
    *-*-mingw*|*-*-cygwin*)
        WINDOWS_SOCKET_LIBS="-lws2_32 -lmswsock"
        ;;
esac
AC_SUBST([WINDOWS_SOCKET_LIBS])

AC_CONFIG_FILES([Makefile libconfig/Makefile libsmb2/Makefile libsqlite3/Makefile libaresq/Makefile aresqc/Makefile])
AC_OUTPUT
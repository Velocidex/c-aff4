AC_DEFUN([URIPARSER_CHECK],
  [
URIPARSER_MISSING="Please install uriparser 0.6.4 or later.
   On a Debian-based system enter 'sudo apt-get install liburiparser-dev'."
AC_CHECK_LIB(uriparser, uriParseUriA,, AC_MSG_ERROR(${URIPARSER_MISSING}))
AC_CHECK_HEADER(uriparser/Uri.h,, AC_MSG_ERROR(${URIPARSER_MISSING}))

URIPARSER_TOO_OLD="uriparser 0.6.4 or later is required, your copy is too old."
AC_COMPILE_IFELSE([AC_LANG_SOURCE([
#include <uriparser/Uri.h>
#if (defined(URI_VER_MAJOR) && defined(URI_VER_MINOR) && defined(URI_VER_RELEASE) \
&& ((URI_VER_MAJOR > 0) \
|| ((URI_VER_MAJOR == 0) && (URI_VER_MINOR > 6)) \
|| ((URI_VER_MAJOR == 0) && (URI_VER_MINOR == 6) && (URI_VER_RELEASE >= 4)) \
))
/* FINE */
#else
# error uriparser not recent enough
#endif
],,AC_MSG_ERROR(${URIPARSER_TOO_OLD}))

])
])

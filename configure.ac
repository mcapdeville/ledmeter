AC_CONFIG_MACRO_DIR([m4])
ACLOCAL_AMFLAGS=-I m4

AC_INIT(ledmeter.c)
AM_INIT_AUTOMAKE(ledmeter, 0.1)


AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S 
AM_PROG_LIBTOOL
AC_CHECK_LIB([asound], [snd_pcm_meter_add_scope], , [AC_MSG_ERROR(You need libasound installed)])
AC_OUTPUT(Makefile)

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib \
	-I$(top_srcdir)/src/lib-mail \
	-I$(top_srcdir)/src/lib-imap \
	-I$(top_srcdir)/src/lib-storage \
	-I$(top_srcdir)/src/imap

imap_moduledir = $(moduledir)/imap

lib10_xexec_plugin_la_LDFLAGS = -module -avoid-version

imap_module_LTLIBRARIES = \
	lib10_xexec_plugin.la

lib10_xexec_plugin_la_SOURCES = \
	cmd-xexec.c \
        xexec_plugin.c

noinst_HEADERS = \
	xexec.h \
	xexec-plugin.h

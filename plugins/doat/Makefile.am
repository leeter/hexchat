include $(top_srcdir)/common.mk
libdir = $(hexchatlibdir)

AM_CPPFLAGS += -I$(top_srcdir)/src/common

lib_LTLIBRARIES = doat.la
doat_la_SOURCES = doat.c
doat_la_LDFLAGS = $(PLUGIN_LDFLAGS) -module

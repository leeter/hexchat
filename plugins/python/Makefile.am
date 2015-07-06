include $(top_srcdir)/common.mk
libdir = $(hexchatlibdir)

AM_CPPFLAGS += $(PY_CFLAGS) $(GLIB_CFLAGS) -I$(top_srcdir)/src/common

lib_LTLIBRARIES = python.la
python_la_SOURCES = python.cpp
python_la_LDFLAGS = $(PLUGIN_LDFLAGS) -module
python_la_LIBADD = $(PY_LIBS) $(GLIB_LIBS)

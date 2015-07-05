include $(top_srcdir)/common.mk
libdir = $(hexchatlibdir)

AM_CPPFLAGS += $(COMMON_CFLAGS) -I$(srcdir)/../../src/common $(LIBPCI_CFLAGS)

lib_LTLIBRARIES = sysinfo.la
sysinfo_la_SOURCES = match.cpp parse.cpp pci.cpp xsys.cpp
sysinfo_la_LDFLAGS = -avoid-version -module $(BOOST_FILESYSTEM_LDFLAGS)
sysinfo_la_LIBADD = $(LIBPCI_LIBS) $(BOOST_FILESYSTEM_LIBS)


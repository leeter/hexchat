libdir = $(hexchatlibdir)

AM_CPPFLAGS = $(COMMON_CFLAGS) -I$(srcdir)/../../src/common $(CPPFLAGS) -std=c++0x -Wall -Wextra -pedantic

lib_LTLIBRARIES = sysinfo.la
sysinfo_la_SOURCES = match.cpp parse.cpp pci.cpp xsys.cpp
sysinfo_la_LDFLAGS = -avoid-version -module $(BOOST_FILESYSTEM_LDFLAGS)
sysinfo_la_LIBADD = $(LIBPCI_LIBS) $(BOOST_FILESYSTEM_LIBS)
sysinfo_la_CPPFLAGS = $(LIBPCI_CFLAGS) $(AM_CPPFLAGS)

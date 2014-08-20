libdir = $(hexchatlibdir)

AM_CPPFLAGS = $(COMMON_CFLAGS) -I$(srcdir)/../../src/common $(CPPFLAGS) -std=c++0x -Wall -Wextra -pedantic

lib_LTLIBRARIES = sysinfo.la
sysinfo_la_SOURCES = hwmon.cpp match.cpp parse.cpp pci.cpp xsys.cpp
sysinfo_la_LDFLAGS = -avoid-version -module
sysinfo_la_LIBADD = -lpci
sysinfo_la_CPPFLAGS = $(AM_CPPFLAGS)

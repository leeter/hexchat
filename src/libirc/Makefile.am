## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libirc.a

AM_CPPFLAGS = $(COMMON_CFLAGS) $(CPPFLAGS) -std=c++0x -Wall -Wextra -pedantic $(BOOST_CPPFLAGS) -I$(top_srcdir)

EXTRA_DIST = \
    tcp_connection.hpp

libirc_a_SOURCES = tcp_connection.cpp
libirc_a_CFLAGS = $(LIBPROXY_CFLAGS) $(CPPFLAGS)
libirc_a_LIBS = $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS) $(BOOST_SYSTEM_LIBS) $(BOOST_ASIO_LIBS) $(BOOST_SIGNALS2_LIBS)

CLEANFILES = $(BUILT_SOURCES)

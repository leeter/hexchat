## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libirc.a

AM_CPPFLAGS = $(COMMON_CFLAGS) $(CPPFLAGS) -std=c++0x -Wall -Wextra -pedantic $(BOOST_CPPFLAGS) -I$(top_srcdir) -D_FORTIFY_SOURCE=2

EXTRA_DIST = \
    connection.hpp \
    server.hpp \
    tcp_connection.hpp \
    tcpfwd.hpp

libirc_a_SOURCES = tcp_connection.cpp throttled_queue.cpp server.cpp
libirc_a_CFLAGS = $(LIBPROXY_CFLAGS) $(CPPFLAGS)
libirc_a_LIBS = $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS) $(BOOST_SYSTEM_LIBS) $(BOOST_ASIO_LIBS) $(BOOST_SIGNALS2_LIBS)

CLEANFILES = $(BUILT_SOURCES)

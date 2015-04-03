## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libirc.a

AM_CPPFLAGS = $(COMMON_CFLAGS) $(CPPFLAGS) -std=c++0x -Wall -Wextra -pedantic $(BOOST_CPPFLAGS) -I$(top_srcdir) -D_FORTIFY_SOURCE=2

EXTRA_DIST = \
    connection.hpp \
    connection_fwd.hpp \
    irc_proto.hpp \
    message.hpp \
    message_fwd.hpp \
    server.hpp \
    tcp_connection.hpp \
    tcpfwd.hpp

libirc_a_SOURCES = irc_proto.cpp message.cpp server.cpp tcp_connection.cpp throttled_queue.cpp
libirc_a_CFLAGS = $(LIBPROXY_CFLAGS) $(CPPFLAGS)
libirc_a_LIBS = $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS) $(BOOST_SYSTEM_LIBS) $(BOOST_ASIO_LIBS) $(BOOST_SIGNALS2_LIBS)

CLEANFILES = $(BUILT_SOURCES)

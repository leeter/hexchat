## Process this file with automake to produce Makefile.in
include $(top_srcdir)/common.mk

noinst_LIBRARIES = libirc.a

EXTRA_DIST = \
    connection.hpp \
    connection_fwd.hpp \
    irc_proto.hpp \
    message.hpp \
    message_fwd.hpp \
    server.hpp \
    tcp_connection.hpp \
    tcpfwd.hpp

libirc_a_SOURCES = detail/inbound.cpp irc_proto.cpp message.cpp server.cpp tcp_connection.cpp throttled_queue.cpp
libirc_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)

CLEANFILES = $(BUILT_SOURCES)

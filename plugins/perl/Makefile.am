include $(top_srcdir)/common.mk
EXTRA_DIST=generate_header lib/HexChat.pm lib/Xchat.pm lib/HexChat/Embed.pm lib/HexChat/List/Network.pm \
	lib/HexChat/List/Network/Entry.pm lib/HexChat/List/Network/AutoJoin.pm lib/IRC.pm

libdir = $(hexchatlibdir)

AM_CPPFLAGS += $(PERL_CFLAGS) -I$(top_srcdir)/src/common

lib_LTLIBRARIES = perl.la
perl_la_SOURCES = perl.cpp
perl_la_LDFLAGS = $(PERL_LDFLAGS) $(PLUGIN_LDFLAGS) -module
perl_la_LIBADD = $(PERL_LIBS)

BUILT_SOURCES = hexchat.pm.h irc.pm.h
CLEANFILES = $(BUILT_SOURCES)

hexchat.pm.h irc.pm.h: lib/HexChat.pm lib/Xchat.pm lib/HexChat/Embed.pm \
	lib/HexChat/List/Network.pm lib/HexChat/List/Network/Entry.pm \
	lib/HexChat/List/Network/AutoJoin.pm lib/IRC.pm
	cd $(srcdir); perl generate_header

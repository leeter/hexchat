## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libhexchatcommon.a

AM_CPPFLAGS = $(COMMON_CFLAGS) $(CPPFLAGS) -std=c++0x -Wall -Wextra -pedantic

EXTRA_DIST = \
	cfgfiles.hpp \
	chanopt.h \
	ctcp.hpp \
	dcc.hpp \
	fe.hpp \
	hexchat.hpp \
	hexchatc.hpp \
	hexchat-plugin.h \
	history.hpp \
	identd.cpp \
	ignore.hpp \
	inbound.hpp \
	inet.h \
	make-te.c \
	modes.h \
	msproxy.h \
	network.h \
	notify.hpp \
	outbound.hpp \
	plugin.h \
	plugin-timer.hpp \
	proto-irc.hpp \
	server.h \
	servlist.h \
	ssl.hpp \
	ssl.cpp	\
	text.hpp \
	textenums.h \
	textevents.h \
	textevents.in \
	tree.hpp \
	url.hpp \
	userlist.hpp \
	util.hpp

if USE_OPENSSL
ssl_c = ssl.cpp
endif

if USE_MSPROXY
msproxy_c = msproxy.cpp
endif

if USE_DBUS
dbusdir = dbus
libhexchatcommon_a_LIBADD =				\
	$(top_builddir)/src/common/dbus/dbus-*.$(OBJEXT)
endif
SUBDIRS = $(dbusdir) .

if DO_PLUGIN
include_HEADERS = hexchat-plugin.h
endif

noinst_PROGRAMS = make-te

libhexchatcommon_a_SOURCES = cfgfiles.cpp chanopt.cpp ctcp.cpp dcc.cpp hexchat.cpp \
	history.cpp ignore.cpp inbound.cpp marshal.c modes.cpp $(msproxy_c) network.cpp notify.cpp \
	outbound.c plugin.c plugin-timer.c proto-irc.c server.c servlist.c \
	$(ssl_c) text.cpp tree.cpp url.cpp userlist.cpp util.cpp
libhexchatcommon_a_CFLAGS = $(LIBPROXY_CFLAGS)

textenums.h: textevents.h

textevents.h: $(srcdir)/textevents.in make-te
	$(AM_V_GEN) ./make-te < $< > $@ 2> textenums.h

marshal.h: $(srcdir)/marshalers.list
	$(AM_V_GEN) $(GLIB_GENMARSHAL) --prefix=_hexchat_marshal --header $< > $@

marshal.c: $(srcdir)/marshalers.list
	$(AM_V_GEN) $(GLIB_GENMARSHAL) --prefix=_hexchat_marshal --body $< > $@


BUILT_SOURCES = textenums.h textevents.h marshal.c marshal.h

CLEANFILES = $(BUILT_SOURCES)

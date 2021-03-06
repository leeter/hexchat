## Process this file with automake to produce Makefile.in
include $(top_srcdir)/common.mk
include $(top_srcdir)/m4/clang-analyze.am

noinst_LIBRARIES = libhexchatcommon.a

EXTRA_DIST = \
	base64.hpp \
	cfgfiles.hpp \
	chanopt.hpp \
	ctcp.hpp \
	dcc.hpp \
	fe.hpp \
	filesystem.hpp\
	glist_iterators.hpp \
	hexchat.hpp \
	hexchatc.hpp \
	hexchat-plugin.h \
	history.hpp \
	identd.cpp \
	ignore.hpp \
	inbound.hpp \
	inet.hpp \
	make-te.cpp \
	modes.hpp \
	network.hpp \
	notify.hpp \
	outbound.hpp \
	plugin.h \
	plugin-timer.hpp \
	proto-irc.hpp \
	sasl.hpp \
	server.hpp \
	servlist.hpp \
	session.hpp \
	sessfwd.hpp \
	session_logging.hpp \
	ssl.hpp \
	ssl.cpp	\
	text.hpp \
	textenums.h \
	textevents.h \
	textevents.in \
	url.hpp \
	userlist.hpp \
	util.hpp

if USE_OPENSSL
ssl_c = ssl.cpp
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

make_te_SOURCES = make-te.cpp

libhexchatcommon_a_SOURCES = base64.cpp cfgfiles.cpp chanopt.cpp ctcp.cpp dcc.cpp filesystem.cpp hexchat.cpp \
	history.cpp ignore.cpp inbound.cpp marshal.c modes.cpp network.cpp notify.cpp \
	outbound.cpp plugin.cpp plugin-timer.cpp proto-irc.cpp sasl.cpp session.cpp session_logging.cpp server.cpp servlist.cpp \
	$(ssl_c) text.cpp url.cpp userlist.cpp util.cpp
libhexchatcommon_a_CPPFLAGS = $(AM_CPPFLAGS) $(COMMON_CFLAGS) $(LIBPROXY_CFLAGS) \
 -I$(top_srcdir) -I../libirc
libhexchatcommon_a_CFLAGS = $(AM_CFLAGS) $(COMMON_CFLAGS) $(LIBPROXY_CFLAGS) -I$(top_srcdir)
#libhexchatcommon_a_LIBADD = ../libirc/libirc.a $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS) $(BOOST_SYSTEM_LIBS) $(BOOST_ASIO_LIBS) $(BOOST_REGEX_LIBS) $(BOOST_SIGNALS2_LIBS) $(BOOST_CHRONO_LIBS)

textenums.h: textevents.h

textevents.h: $(srcdir)/textevents.in make-te
	$(AM_V_GEN) ./make-te < $< > $@ 2> textenums.h

marshal.h: $(srcdir)/marshalers.list
	$(AM_V_GEN) $(GLIB_GENMARSHAL) --prefix=_hexchat_marshal --header $< > $@

marshal.c: $(srcdir)/marshalers.list
	$(AM_V_GEN) $(GLIB_GENMARSHAL) --prefix=_hexchat_marshal --body $< > $@


if DO_STATIC_ANALYSIS
analyze_plists = $(libhexchatcommon_a_SOURCES:%.c=%.plist)
all-local: $(analyze_plists)
MOSTLYCLEANFILES = $(analyze_plists)
endif

BUILT_SOURCES = textenums.h textevents.h marshal.c marshal.h

CLEANFILES = $(BUILT_SOURCES)

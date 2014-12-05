localedir = $(datadir)/locale

bin_PROGRAMS = hexchat

AM_CPPFLAGS = $(BOOST_CPPFLAGS) $(GUI_CFLAGS) $(CPPFLAGS) -std=c++0x -Wall -Wextra -pedantic -DLOCALEDIR=\"$(localedir)\"

hexchat_LDADD = ../common/libhexchatcommon.a ../libirc/libirc.a $(GUI_LIBS) $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS) $(BOOST_SYSTEM_LIBS) $(BOOST_ASIO_LIBS) $(BOOST_SIGNALS2_LIBS) $(BOOST_THREAD_LIBS) $(BOOST_REGEX_LIBS) $(BOOST_CHRONO_LIBS)

EXTRA_DIST = \
	ascii.hpp banlist.hpp chanlist.hpp chanview.hpp \
	custom-list.hpp editlist.hpp fe-gtk.hpp fkeys.hpp gtkutil.hpp joind.hpp \
	maingui.hpp menu.hpp notifygui.hpp palette.hpp pixmaps.hpp \
	plugin-tray.hpp plugingui.cpp plugingui.hpp rawlog.hpp sexy-iso-codes.hpp \
	sexy-spell-entry.hpp textgui.hpp urlgrab.hpp userlistgui.hpp xtext.hpp \
	../../data/hexchat.gresource.xml

BUILT_SOURCES = resources.c

CLEANFILES = $(BUILT_SOURCES)

if DO_PLUGIN
plugingui_c = plugingui.cpp
endif

if HAVE_ISO_CODES
iso_codes_c = sexy-iso-codes.cpp
endif

hexchat_SOURCES = ascii.cpp banlist.cpp chanlist.cpp chanview.cpp custom-list.cpp \
	dccgui.cpp editlist.cpp fe-gtk.cpp fkeys.cpp gtkutil.cpp ignoregui.cpp joind.cpp menu.cpp \
	maingui.cpp notifygui.cpp palette.cpp pixmaps.cpp plugin-tray.cpp $(plugingui_c) \
	rawlog.cpp resources.c servlistgui.cpp setup.cpp $(iso_codes_c) \
	sexy-spell-entry.cpp textgui.cpp urlgrab.cpp userlistgui.cpp xtext.cpp
hexchat_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_builddir)/src/common -I$(top_builddir)/src/libirc
hexchat_LDFLAGS = $(BOOST_FILESYSTEM_LDFLAGS) $(BOOST_IOSTREAMS_LDFLAGS) $(BOOST_SYSTEM_LDFLAGS) $(BOOST_ASIO_LDFLAGS) $(BOOST_SIGNALS2_LDFLAGS) $(BOOST_REGEX_LDFLAGS) $(BOOST_THREAD_LDFLAGS) $(BOOST_CHRONO_LDFLAGS)
hexchat_LIBS = $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS) $(BOOST_SYSTEM_LIBS) $(BOOST_ASIO_LIBS) $(BOOST_SIGNALS2_LIBS) $(BOOST_THREAD_LIBS) $(BOOST_REGEX_LIBS)

resources.c: $(top_srcdir)/data/hexchat.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(top_srcdir)/data --generate-dependencies $(top_srcdir)/data/hexchat.gresource.xml)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(top_srcdir)/data --generate-source $<

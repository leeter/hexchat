localedir = $(datadir)/locale

bin_PROGRAMS = hexchat-text

AM_CPPFLAGS = $(COMMON_CFLAGS) -DLOCALEDIR=\"$(localedir)\" $(BOOST_CPPFLAGS) $(CPPFLAGS) -std=c++0x -Wall -Wextra -pedantic

hexchat_text_LDADD = ../common/libhexchatcommon.a $(COMMON_LIBS) $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS) $(BOOST_SYSTEM_LIBS)
hexchat_text_SOURCES = fe-text.cpp fe-text.h
hexchat_text_LDFLAGS = $(BOOST_FILESYSTEM_LDFLAGS) $(BOOST_IOSTREAMS_LDFLAGS) $(BOOST_SYSTEM_LDFLAGS)
hexchat_text_LIBS = $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS) $(BOOST_SYSTEM_LIBS)

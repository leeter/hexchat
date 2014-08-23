localedir = $(datadir)/locale

bin_PROGRAMS = hexchat-text

AM_CPPFLAGS = $(COMMON_CFLAGS) -DLOCALEDIR=\"$(localedir)\" $(BOOST_CPPFLAGS) $(CPPFLAGS) -std=c++0x -Wall -Wextra -pedantic

hexchat_text_LDADD = ../common/libhexchatcommon.a $(COMMON_LIBS) $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS)
hexchat_text_SOURCES = fe-text.cpp fe-text.h

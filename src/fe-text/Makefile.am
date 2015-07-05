include $(top_srcdir)/common.mk
localedir = $(datadir)/locale

bin_PROGRAMS = hexchat-text

AM_CPPFLAGS += $(COMMON_CFLAGS) -DLOCALEDIR=\"$(localedir)\" -I$(top_builddir)/src/common -I$(top_builddir)/src/libirc

hexchat_text_LDADD = ../common/libhexchatcommon.a ../libirc/libirc.a \
 $(COMMON_LIBS) $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS) \
 $(BOOST_SYSTEM_LIBS) $(BOOST_ASIO_LIBS) $(BOOST_SIGNALS2_LIBS) \
 $(BOOST_REGEX_LIBS) $(BOOST_THREAD_LIBS) $(BOOST_CHRONO_LIBS)
hexchat_text_SOURCES = fe-text.cpp fe-text.h
hexchat_text_LDFLAGS = -Wl,-z,relro,-z,now $(BOOST_FILESYSTEM_LDFLAGS) \
 $(BOOST_IOSTREAMS_LDFLAGS) $(BOOST_SYSTEM_LDFLAGS) \
 $(BOOST_ASIO_LDFLAGS) $(BOOST_SIGNALS2_LDFLAGS) \
 $(BOOST_THREAD_LDFLAGS) $(BOOST_REGEX_LDFLAGS) \
 $(BOOST_CHRONO_LDFLAGS)


AM_CPPFLAGS = $(COMMON_CFLAGS) $(CPPFLAGS) -std=c++0x -Wall -Wextra -pedantic $(BOOST_CPPFLAGS) -I$(top_srcdir) -I../../src/libirc -D_FORTIFY_SOURCE=2
AM_CFLAGS = $(COMMON_CFLAGS) $(CFLAGS) -std=c1x -Wall -Wextra -pedantic -I$(top_srcdir) -I../libirc -D_FORTIFY_SOURCE=2

noinst_PROGRAMS = libirctest
libirctest_SOURCES = irc_proto_test.cpp
libirctest_LIBS = ../../src/libirc/libirc.a $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS) $(BOOST_SYSTEM_LIBS) $(BOOST_ASIO_LIBS) $(BOOST_REGEX_LIBS) $(BOOST_SIGNALS2_LIBS) $(BOOST_CHRONO_LIBS)

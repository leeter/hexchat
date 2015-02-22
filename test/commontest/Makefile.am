AM_CPPFLAGS = $(COMMON_CFLAGS) $(CPPFLAGS) -std=c++0x -Wall -Wextra -pedantic $(BOOST_CPPFLAGS) -I$(top_srcdir) -I../../src/libirc -I../../src/common -D_FORTIFY_SOURCE=2
AM_CFLAGS = $(COMMON_CFLAGS) $(CFLAGS) -std=c1x -Wall -Wextra -pedantic -I$(top_srcdir) -I../../src/libirc -I../../src/common -D_FORTIFY_SOURCE=2

noinst_PROGRAMS = libhexchatcommon-test
libhexchatcommon_test_SOURCES = fe_stub.cpp util_test.cpp
libhexchatcommon_test_LDADD = ../../src/libirc/libirc.a ../../src/common/libhexchatcommon.a $(COMMON_LIBS) $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS) $(BOOST_SYSTEM_LIBS) $(BOOST_ASIO_LIBS) $(BOOST_REGEX_LIBS) $(BOOST_SIGNALS2_LIBS) $(BOOST_CHRONO_LIBS) $(BOOST_UNIT_TEST_FRAMEWORK_LIBS)
libhexchatcommon_test_LDFLAGS = -Wl,-z,relro,-z,now $(BOOST_FILESYSTEM_LDFLAGS) $(BOOST_IOSTREAMS_LDFLAGS) $(BOOST_SYSTEM_LDFLAGS) $(BOOST_ASIO_LDFLAGS) $(BOOST_SIGNALS2_LDFLAGS) $(BOOST_THREAD_LDFLAGS) $(BOOST_REGEX_LDFLAGS) $(BOOST_CHRONO_LDFLAGS) $(BOOST_UNIT_TEST_FRAMEWORK_LDFLAGS)

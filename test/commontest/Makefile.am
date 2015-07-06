include $(top_srcdir)/common.mk

AM_CPPFLAGS += $(COMMON_CFLAGS) -I../../src/libirc -I../../src/common

noinst_PROGRAMS = libhexchatcommon-test
libhexchatcommon_test_SOURCES = fe_stub.cpp plugintest.cpp util_test.cpp
libhexchatcommon_test_LDADD = ../../src/common/libhexchatcommon.a ../../src/libirc/libirc.a $(COMMON_LIBS) \
  $(BOOST_FILESYSTEM_LIBS) $(BOOST_IOSTREAMS_LIBS) $(BOOST_SYSTEM_LIBS) $(BOOST_ASIO_LIBS) $(BOOST_REGEX_LIBS) \
  $(BOOST_SIGNALS2_LIBS) $(BOOST_CHRONO_LIBS) $(BOOST_UNIT_TEST_FRAMEWORK_LIBS)
libhexchatcommon_test_LDFLAGS = -Wl,-z,relro,-z,now $(BOOST_FILESYSTEM_LDFLAGS) \
  $(BOOST_IOSTREAMS_LDFLAGS) $(BOOST_SYSTEM_LDFLAGS) $(BOOST_ASIO_LDFLAGS) \
  $(BOOST_SIGNALS2_LDFLAGS) $(BOOST_THREAD_LDFLAGS) $(BOOST_REGEX_LDFLAGS) \
  $(BOOST_CHRONO_LDFLAGS) $(BOOST_UNIT_TEST_FRAMEWORK_LDFLAGS)


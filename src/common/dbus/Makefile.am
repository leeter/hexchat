AM_CPPFLAGS = $(COMMON_CFLAGS) $(DBUS_CFLAGS) $(CPPFLAGS) -std=c++0x -Wall -Wextra -pedantic -I$(top_srcdir)/src/common
AM_CFLAGS = $(COMMON_CFLAGS) $(DBUS_CFLAGS) $(CFLAGS) -std=c1x -Wall -Wextra -pedantic -I$(top_srcdir)/src/common

noinst_LIBRARIES = libhexchatdbus.a
libhexchatdbus_a_SOURCES =			\
	dbus-plugin.c				\
	dbus-plugin.h				\
	dbus-client.cpp				\
	dbus-client.h


EXTRA_DIST =				\
	remote-object.xml		\
	example.py			\
	org.hexchat.service.service.in

BUILT_SOURCES =				\
	remote-object-glue.h

CLEANFILES = $(BUILT_SOURCES)



noinst_PROGRAMS = example
example_SOURCES = example.c
example_LDADD = $(DBUS_LIBS) $(GLIB_LIBS)

remote-object-glue.h: remote-object.xml
	$(AM_V_GEN) $(LIBTOOL) --mode=execute $(DBUS_BINDING_TOOL) --prefix=remote_object --mode=glib-server --output=$@ $<

# Dbus service file
servicedir = $(DBUS_SERVICES_DIR)
service_in_files = org.hexchat.service.service.in
service_DATA = $(service_in_files:.service.in=.service)

# Rule to make the service file with bindir expanded
$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@bindir\@|$(bindir)|" $< > $@

## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

EXTRA_DIST =						\
	org.cafe.PowerManager.xml			\
	org.cafe.PowerManager.Backlight.xml		\
	org.cafe.PowerManager.KbdBacklight.xml		\
	cpm-marshal.list				\
	$(NULL)

AM_CPPFLAGS =						\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(CAIRO_CFLAGS)					\
	$(KEYRING_CFLAGS)				\
	$(X11_CFLAGS)					\
	$(LIBNOTIFY_CFLAGS)				\
	$(KANBERRA_CFLAGS)				\
	$(GSTREAMER_CFLAGS)				\
	-DI_KNOW_THE_DEVICEKIT_POWER_API_IS_SUBJECT_TO_CHANGE \
	$(UPOWER_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"			 	\
	-DSBINDIR=\"$(sbindir)\"			\
	-DCAFELOCALEDIR=\""$(datadir)/locale"\"		\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DGPM_ICONS_DATA=\"$(pkgdatadir)/icons\"	\
	-DCTKBUILDERDIR=\"$(pkgdatadir)\"		\
	-DUP_DISABLE_DEPRECATED				\
	-DEGG_LOG_FILE=\""/tmp/gpm.log"\"		\
	-DEGG_VERBOSE="\"GPM_VERBOSE\""			\
	-DEGG_LOGGING="\"GPM_LOGGING\""			\
	-DEGG_CONSOLE="\"GPM_CONSOLE\""			\
	-I$(top_srcdir)					\
	$(DISABLE_DEPRECATED)

AM_CPPFLAGS += $(NULL)

bin_PROGRAMS =						\
	cafe-power-manager				\
	cafe-power-preferences				\
	cafe-power-statistics				\
	$(NULL)

sbin_PROGRAMS =						\
	cafe-power-backlight-helper			\
	$(NULL)

if HAVE_TESTS
check_PROGRAMS =					\
	cafe-power-self-test
endif

noinst_LIBRARIES = libgpmshared.a
libgpmshared_a_SOURCES =				\
	egg-color.c					\
	egg-color.h					\
	egg-debug.h					\
	egg-debug.c					\
	egg-precision.h					\
	egg-precision.c					\
	egg-array-float.c				\
	egg-array-float.h				\
	egg-idletime.h					\
	egg-idletime.c					\
	egg-discrete.h					\
	egg-discrete.c					\
	egg-console-kit.h				\
	egg-console-kit.c				\
	cpm-common.h					\
	cpm-common.c					\
	cpm-brightness.h				\
	cpm-brightness.c				\
	cpm-marshal.h					\
	cpm-marshal.c					\
	cpm-upower.c					\
	cpm-upower.h

cafe_power_backlight_helper_SOURCES =			\
	cpm-backlight-helper.c				\
	$(NULL)

cafe_power_backlight_helper_LDADD =			\
	libgpmshared.a					\
	$(GLIB_LIBS)					\
	-lm

cafe_power_backlight_helper_CFLAGS =			\
	$(WARN_CFLAGS)					\
	$(NULL)

cafe-power-statistics-resources.h cafe-power-statistics-resources.c: $(srcdir)/../data/org.cafe.power-manager.statistics.gresource.xml Makefile $(shell $(GLIB_COMPILE_RESOURCES) --generate-dependencies --sourcedir $(srcdir)/../data $(srcdir)/../data/org.cafe.power-manager.statistics.gresource.xml)
	$(AM_V_GEN) XMLLINT=$(XMLLINT) $(GLIB_COMPILE_RESOURCES) --target $@ --sourcedir $(srcdir)/../data --generate --c-name statistics $<

nodist_cafe_power_statistics_SOURCES =			\
	cafe-power-statistics-resources.h		\
	cafe-power-statistics-resources.c		\
	$(NULL)

cafe_power_statistics_SOURCES =				\
	cpm-statistics.c				\
	cpm-point-obj.c					\
	cpm-point-obj.h					\
	cpm-graph-widget.h				\
	cpm-graph-widget.c				\
	$(NULL)

cafe_power_statistics_LDADD =				\
	libgpmshared.a					\
	$(GLIB_LIBS)					\
	$(X11_LIBS)					\
	$(UPOWER_LIBS)					\
	$(CAIRO_LIBS)					\
	$(DBUS_LIBS)					\
	-lm

cafe_power_statistics_CFLAGS =				\
	$(WARN_CFLAGS)					\
	$(NULL)

cafe-power-preferences-resources.h cafe-power-preferences-resources.c: $(srcdir)/../data/org.cafe.power-manager.preferences.gresource.xml Makefile $(shell $(GLIB_COMPILE_RESOURCES) --generate-dependencies --sourcedir $(srcdir)/../data $(srcdir)/../data/org.cafe.power-manager.preferences.gresource.xml)
	$(AM_V_GEN) XMLLINT=$(XMLLINT) $(GLIB_COMPILE_RESOURCES) --target $@ --sourcedir $(srcdir)/../data --generate --c-name preferences $<

nodist_cafe_power_preferences_SOURCES =			\
	cafe-power-preferences-resources.h		\
	cafe-power-preferences-resources.c		\
	$(NULL)

cafe_power_preferences_SOURCES =			\
	cpm-prefs.c					\
	cpm-prefs-core.h				\
	cpm-prefs-core.c				\
	$(NULL)

cafe_power_preferences_LDADD =				\
	libgpmshared.a					\
	$(GLIB_LIBS)					\
	$(X11_LIBS)					\
	$(CAIRO_LIBS)					\
	$(DBUS_LIBS)					\
	$(GPM_EXTRA_LIBS)				\
	$(UPOWER_LIBS)					\
	-lm

cafe_power_preferences_CFLAGS =				\
	$(WARN_CFLAGS)					\
	$(NULL)

cafe-power-manager-resources.h cafe-power-manager-resources.c: $(srcdir)/../data/org.cafe.power-manager.manager.gresource.xml Makefile $(shell $(GLIB_COMPILE_RESOURCES) --generate-dependencies --sourcedir $(srcdir)/../data $(srcdir)/../data/org.cafe.power-manager.manager.gresource.xml)
	$(AM_V_GEN) XMLLINT=$(XMLLINT) $(GLIB_COMPILE_RESOURCES) --target $@ --sourcedir $(srcdir)/../data --generate --c-name preferences $<

nodist_cafe_power_manager_SOURCES =			\
	cafe-power-manager-resources.h			\
	cafe-power-manager-resources.c			\
	$(NULL)

cafe_power_manager_SOURCES =				\
	cpm-dpms.h					\
	cpm-dpms.c					\
	cpm-phone.h					\
	cpm-phone.c					\
	cpm-backlight.h					\
	cpm-backlight.c					\
	cpm-idle.h					\
	cpm-idle.c					\
	cpm-load.h					\
	cpm-load.c					\
	cpm-control.h					\
	cpm-control.c					\
	cpm-button.h					\
	cpm-button.c					\
	cpm-kbd-backlight.h				\
	cpm-kbd-backlight.c				\
	cpm-main.c					\
	cpm-manager.h					\
	cpm-manager.c					\
	cpm-tray-icon.h					\
	cpm-tray-icon.c					\
	cpm-screensaver.h				\
	cpm-screensaver.c				\
	cpm-session.h					\
	cpm-session.c					\
	cpm-networkmanager.h				\
	cpm-networkmanager.c				\
	cpm-icon-names.h				\
	gsd-media-keys-window.h				\
	gsd-media-keys-window.c				\
	msd-osd-window.h				\
	msd-osd-window.c				\
	cpm-engine.h					\
	cpm-engine.c					\
	$(NULL)

cafe_power_manager_LDADD =				\
	libgpmshared.a					\
	$(GLIB_LIBS)					\
	$(X11_LIBS)					\
	$(GSTREAMER_LIBS)				\
	$(CAIRO_LIBS)					\
	$(KEYRING_LIBS)					\
	$(DBUS_LIBS)					\
	$(X11_LIBS)					\
	$(KANBERRA_LIBS)				\
	$(LIBNOTIFY_LIBS)				\
	$(GPM_EXTRA_LIBS)				\
	$(UPOWER_LIBS)					\
	-lm

cafe_power_manager_CFLAGS =				\
	$(WARN_CFLAGS)					\
	$(NULL)

if HAVE_TESTS
cafe_power_self_test_SOURCES =				\
	cpm-self-test.c					\
	egg-color.h					\
	egg-color.c					\
	egg-test.h					\
	egg-test.c					\
	egg-debug.h					\
	egg-debug.c					\
	egg-precision.h					\
	egg-precision.c					\
	egg-idletime.h					\
	egg-idletime.c					\
	egg-discrete.h					\
	egg-discrete.c					\
	egg-array-float.h				\
	egg-array-float.c				\
	egg-console-kit.h				\
	egg-console-kit.c				\
	cpm-control.h					\
	cpm-control.c					\
	cpm-networkmanager.h				\
	cpm-networkmanager.c				\
	cpm-dpms.h					\
	cpm-dpms.c					\
	cpm-button.h					\
	cpm-button.c					\
	cpm-screensaver.h				\
	cpm-screensaver.c				\
	cpm-engine.h					\
	cpm-engine.c					\
	cpm-phone.h					\
	cpm-phone.c					\
	cpm-idle.h					\
	cpm-idle.c					\
	cpm-session.h					\
	cpm-session.c					\
	cpm-load.h					\
	cpm-load.c					\
	cpm-marshal.h					\
	cpm-marshal.c					\
	cpm-common.h					\
	cpm-common.c					\
	cpm-upower.h					\
	cpm-upower.c					\
	$(NULL)

cafe_power_self_test_LDADD =				\
	$(GLIB_LIBS)					\
	$(X11_LIBS)					\
	$(CAIRO_LIBS)					\
	$(KEYRING_LIBS)					\
	$(GSTREAMER_LIBS)				\
	$(UPOWER_LIBS)					\
	$(DBUS_LIBS)					\
	$(X11_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GPM_EXTRA_LIBS)				\
	-lm

cafe_power_self_test_CFLAGS = 				\
	-DEGG_TEST					\
	$(AM_CFLAGS)					\
	$(WARN_CFLAGS)					\
	$(NULL)
endif

BUILT_SOURCES = 					\
	org.cafe.PowerManager.h				\
	org.cafe.PowerManager.Backlight.h		\
	org.cafe.PowerManager.KbdBacklight.h		\
	cpm-marshal.c					\
	cpm-marshal.h					\
	$(NULL)

cpm-marshal.c: cpm-marshal.list
	echo "#include \"cpm-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=gpm_marshal --body >> $@

cpm-marshal.h: cpm-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=gpm_marshal --header > $@

org.cafe.PowerManager.h: org.cafe.PowerManager.xml
	$(LIBTOOL) --mode=execute dbus-binding-tool	\
		--prefix=gpm_manager			\
		--mode=glib-server			\
		--output=org.cafe.PowerManager.h	\
		$(srcdir)/org.cafe.PowerManager.xml

org.cafe.PowerManager.Backlight.h: org.cafe.PowerManager.Backlight.xml
	$(LIBTOOL) --mode=execute dbus-binding-tool	\
		--prefix=gpm_backlight			\
		--mode=glib-server			\
		--output=org.cafe.PowerManager.Backlight.h	\
		$(srcdir)/org.cafe.PowerManager.Backlight.xml

org.cafe.PowerManager.KbdBacklight.h: org.cafe.PowerManager.KbdBacklight.xml
	$(LIBTOOL) --mode=execute dbus-binding-tool	\
		--prefix=gpm_kbd_backlight		\
		--mode=glib-server			\
		--output=org.cafe.PowerManager.KbdBacklight.h	\
		$(srcdir)/org.cafe.PowerManager.KbdBacklight.xml

clean-local:
	rm -f *~
	rm -f cpm-marshal.c cpm-marshal.h

CLEANFILES = \
	$(BUILT_SOURCES)				\
	cafe-power-manager-resources.h			\
	cafe-power-manager-resources.c			\
	cafe-power-preferences-resources.h		\
	cafe-power-preferences-resources.c		\
	cafe-power-statistics-resources.h		\
	cafe-power-statistics-resources.c		\
	$(NULL)

if HAVE_TESTS
TESTS = cafe-power-self-test
endif

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)


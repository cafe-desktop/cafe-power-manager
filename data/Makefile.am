SUBDIRS = icons


desktopdir = $(datadir)/applications
desktop_in_files = cafe-power-preferences.desktop.in \
		   cafe-power-statistics.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
%.desktop: %.desktop.in
	$(AM_V_GEN) $(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@

autostartdir = $(sysconfdir)/xdg/autostart
autostart_in_files = cafe-power-manager.desktop.in
autostart_DATA = $(autostart_in_files:.desktop.in=.desktop)
$(autostart_DATA): $(autostart_in_files)
	$(AM_V_GEN) $(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@

servicedir       = $(DBUS_SERVICES_DIR)
service_in_files = org.cafe.PowerManager.service.in
service_DATA     = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@servicedir\@|$(bindir)|" $< > $@

@GSETTINGS_RULES@
gsettings_schemas_in_files = org.cafe.power-manager.gschema.xml.in
gsettings_SCHEMAS = $(gsettings_schemas_in_files:.xml.in=.xml)

pkgdata_DATA =							\
	acme.ui

man_MANS =							\
	cafe-power-manager.1					\
	cafe-power-backlight-helper.1				\
	cafe-power-statistics.1					\
	cafe-power-preferences.1

EXTRA_DIST =							\
	$(service_in_files)					\
	$(autostart_in_files)					\
	$(desktop_in_files)					\
	$(gsettings_schemas_in_files)				\
	$(pkgdata_DATA)						\
	$(man_MANS)						\
	org.cafe.power-manager.manager.gresource.xml		\
	org.cafe.power-manager.preferences.gresource.xml	\
	org.cafe.power-manager.statistics.gresource.xml		\
	cpm-prefs.ui						\
	cpm-statistics.ui					\
	cafe-power-manager.about

clean-local :
	rm -f *~

DISTCLEANFILES =						\
	cafe-power-manager.desktop				\
	cafe-power-preferences.desktop				\
	cafe-power-statistics.desktop				\
	org.cafe.PowerManager.service				\
	$(gsettings_SCHEMAS)

@YELP_HELP_RULES@

HELP_ID = cafe-power-manager
HELP_FILES = index.docbook legal.xml
HELP_MEDIA = \
	figures/applet-brightness.png \
	figures/applet-inhibit.png \
	figures/cpm-cell-capacity.png \
	figures/cpm-charged.png \
	figures/cpm-critical.png \
	figures/cpm-low.png \
	figures/cpm-prefs-ac.png \
	figures/cpm-prefs-battery.png \
	figures/cpm-prefs-general.png \
	figures/cpm-stats-graph.png \
	figures/cpm-suspend-problem.png \
	figures/cpm-unplugged.png \
	figures/gs-prefs.png

# Add linguas to be ignored, e.g. IGNORE_HELP_LINGUAS = ca de es fr
IGNORE_HELP_LINGUAS =
HELP_LINGUAS = $(if $(IGNORE_HELP_LINGUAS), \
	$(filter-out $(IGNORE_HELP_LINGUAS),$(subst /,,$(dir $(wildcard */*.po)))), \
	$(subst /,,$(dir $(wildcard */*.po))) )

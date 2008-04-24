SUBDIRS = \
	po \
	themes \
	xfce4-notifyd \
	xfce4-notifyd-config

EXTRA_DIST = \
	TODO \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update

AUTOMAKE_OPTIONS = \
	1.8 \
	dist-bzip2

distclean-local:
	-rm -rf *.cache *~

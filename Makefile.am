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

checksums: distcheck
	sha1sum $(PACKAGE)-$(VERSION).tar.bz2 > $(PACKAGE)-$(VERSION).tar.bz2.sha1.asc

gpg-sign: checksums
	gpg -b --armor $(PACKAGE)-$(VERSION).tar.bz2

release: gpg-sign
	-mkdir releases/
	mv $(PACKAGE)-$(VERSION).tar.bz2* releases/
	-rm $(PACKAGE)-$(VERSION).tar.gz

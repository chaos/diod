man5_MANS = \
	diod.conf.5

install-data-local:
	$(top_srcdir)/config/install-sh -m 644 $(srcdir)/diod.conf \
		$(DESTDIR)$(sysconfdir)/diod.conf

uninstall-local:
	$(RM) $(DESTDIR)$(sysconfdir)/diod.conf

EXTRA_DIST = diod.conf $(man5_MANS)

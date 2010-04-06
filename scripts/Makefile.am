install-data-local:
	$(top_srcdir)/config/install-sh -m 755 $(srcdir)/diodctl.init \
		$(DESTDIR)$(sysconfdir)/init.d/diodctl

uninstall-local:
	$(RM) $(DESTDIR)$(sysconfdir)/init.d/diodctl

EXTRA_DIST = diodctl.init

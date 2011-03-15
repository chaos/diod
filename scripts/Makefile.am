install-data-local:
	$(top_srcdir)/config/install-sh -m 755 $(srcdir)/diodctl.init \
		$(DESTDIR)$(sysconfdir)/init.d/diodctl
	$(top_srcdir)/config/install-sh -m 755 $(srcdir)/auto.g \
		$(DESTDIR)$(sysconfdir)/auto.g

uninstall-local:
	$(RM) $(DESTDIR)$(sysconfdir)/init.d/diodctl
	$(RM) $(DESTDIR)$(sysconfdir)/auto.g

EXTRA_DIST = diodctl.init auto.g

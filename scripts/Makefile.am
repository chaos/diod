install-data-local:
	$(top_srcdir)/config/install-sh -m 755 $(srcdir)/diod.init \
		$(DESTDIR)$(sysconfdir)/init.d/diod

uninstall-local:
	$(RM) $(DESTDIR)$(sysconfdir)/init.d/diod

EXTRA_DIST = diod.init

install-data-local:
	$(top_srcdir)/config/install-sh -m 755 $(srcdir)/diod.init \
		$(DESTDIR)$(sysconfdir)/init.d/diod
	$(top_srcdir)/config/install-sh -m 755 $(srcdir)/auto.diod \
		$(DESTDIR)$(sysconfdir)/auto.diod

uninstall-local:
	$(RM) $(DESTDIR)$(sysconfdir)/init.d/diod
	$(RM) $(DESTDIR)$(sysconfdir)/auto.diod

EXTRA_DIST = diod.init auto.diod

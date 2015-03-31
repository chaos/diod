systemddir=$(sysconfdir)/systemd/system

install-data-local:
	$(top_srcdir)/config/install-sh -m 755 $(srcdir)/auto.diod \
		$(DESTDIR)$(sysconfdir)/auto.diod
	$(top_srcdir)/config/install-sh -m 755 $(srcdir)/diod.service \
		$(DESTDIR)$(systemddir)/diod.service

uninstall-local:
	$(RM) $(DESTDIR)$(sysconfdir)/auto.diod
	$(RM) $(DESTDIR)$(systemddir)/diod.service

EXTRA_DIST = diod.init auto.diod diod.service

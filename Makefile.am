SUBDIRS = \
	src \
	man \
	etc \
	t \
	tests

EXTRA_DIST = diod.spec

export DEB_BUILD_OPTIONS ?= nocheck terse
deb: debian scripts/debbuild.sh
	+@$(top_srcdir)/scripts/debbuild.sh $(abs_top_srcdir)

SUBDIRS = \
	src \
	man \
	etc \
	t

EXTRA_DIST = \
	NEWS.md

export DEB_BUILD_OPTIONS ?= nocheck terse
deb: debian scripts/debbuild.sh
	+@$(top_srcdir)/scripts/debbuild.sh $(abs_top_srcdir)

SUBDIRS = \
	src \
	man \
	etc

if ENABLE_TESTS
  SUBDIRS += tests
endif

EXTRA_DIST = diod.spec

export DEB_BUILD_OPTIONS ?= nocheck terse
deb: debian scripts/debbuild.sh
	+@$(top_srcdir)/scripts/debbuild.sh $(abs_top_srcdir)

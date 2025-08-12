AM_TESTS_ENVIRONMENT =

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

clean-local:
	rm -fr trash-directory.* test-results .prove *.output *.diod.log

# This list is included in both TESTS and dist_check_SCRIPTS.
TESTSCRIPTS = \
	t0000-sharness.t \
	t1000-valgrind.t \
	t0001-basic-runasuser.t \
	t0002-basic-allsquash.t \
	t0003-basic-multiuser.t \
	t0004-npclient-basic.t \
	t0005-npclient-xattr.t

# make check runs these TAP tests directly (both scripts and programs)
TESTS = \
	$(TESTSCRIPTS)

dist_check_SCRIPTS = \
	$(TESTSCRIPTS)

EXTRA_DIST = \
	sharness.sh \
	aggregate-results.sh \
	sharness.d


AM_CFLAGS = @WARNING_CFLAGS@

AM_CPPFLAGS = \
	-I$(top_srcdir)

common_ldadd =

check_PROGRAMS =

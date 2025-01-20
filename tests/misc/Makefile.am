check_PROGRAMS =

TESTS_ENVIRONMENT = env
TESTS_ENVIRONMENT += "MISC_SRCDIR=$(top_srcdir)/tests/misc"
TESTS_ENVIRONMENT += "MISC_BUILDDIR=$(top_builddir)/tests/misc"
TESTS_ENVIRONMENT += "TOP_SRCDIR=$(top_srcdir)"
TESTS_ENVIRONMENT += "TOP_BUILDDIR=$(top_builddir)"

TESTS = t15

CLEANFILES = *.out *.diod

EXTRA_DIST = $(TESTS) memcheck valgrind.supp

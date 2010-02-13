check_PROGRAMS = tsetuid tsetfsuid

TESTS_ENVIRONMENT = env
TESTS_ENVIRONMENT += "PATH_DIOD=$(top_builddir)/src/diod"
TESTS = t00

CLEANFILES = *.out *.diff

AM_CFLAGS = -I$(top_srcdir)/src

tsetuid_SOURCES = tsetuid.c
tsetuid_LDADD = -lpthread
tsetfsuid_SOURCES = tsetfsuid.c
tsetfsuid_LDADD = -lpthread

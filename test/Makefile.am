TESTS = run-test.sh
TESTS_ENVIRONMENT = NO_MAKE=yes CUTTER="$(CUTTER)"

noinst_LTLIBRARIES = test_pgnumeric.la

INCLUDES = $(CUTTER_CFLAGS) -I$(top_srcdir)/src
LIBS = $(CUTTER_LIBS) $(top_builddir)/src/libpgnumeric.la

LDFLAGS = -module -rpath $(libdir) -avoid-version -no-undefined
test_pgnumeric_la_SOURCES = test_pgnumeric.c

echo-cutter:
	@echo $(CUTTER)

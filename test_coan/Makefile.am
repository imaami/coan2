if MAKE_CHECK_TIMING
TIMING_METRICS_ENABLED = 1
else
TIMING_METRICS_ENABLED = 0
endif

noinst_PROGRAMS = wordsize
wordsize_SOURCES = wordsize.cpp
check_PROGRAMS = coan_api_tester
coan_api_tester_SOURCES = coan_api_tester.cpp
coan_api_tester_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src
coan_api_tester_LDADD = $(top_builddir)/src/libcoan.a

CORE_TESTS = coan_api_tester coan_c23_tester.py coan_case_tester.py coan_bulk_tester.py coan_spin_tester.py \
coan_symbol_rewind_tester.py coan_softlink_tester.py
if MAKE_CHECK_TIMING
TESTS = $(CORE_TESTS) coan_test_metrics.py
# Aggregate metrics only after the individual suites have written them.
coan_test_metrics.log: coan_api_tester.log coan_c23_tester.log coan_case_tester.log \
coan_bulk_tester.log coan_spin_tester.log coan_symbol_rewind_tester.log coan_softlink_tester.log
else
TESTS = $(CORE_TESTS)
endif

AM_TESTS_ENVIRONMENT = COAN_PKGDIR=$(top_srcdir); \
	COAN_BUILDDIR=$(top_builddir); TIMING_METRICS=$(TIMING_METRICS_ENABLED); \
	rm -f coan.test_timer.time.txt; \
	export COAN_PKGDIR; export COAN_BUILDDIR; export TIMING_METRICS;
TEST_EXTENSIONS = .py
PY_LOG_COMPILER = "$(PYTHON)"

# Automake owns the test logs; make check VERBOSE=1 prints failures without
# starting a second run through a direct check dependency on test-suite.log.

.PHONY: clean-metrics

clean-metrics:
	rm -f *.time.txt *.size.txt
	
check-am: clean-metrics
	
clean: clean-metrics
	rm -f *.o wordsize *.tmp
	cd $(top_srcdir)/test_coan/test_cases && \
		rm -f *.stderr *.stdout *.output coan_out_*

distclean-local: clean


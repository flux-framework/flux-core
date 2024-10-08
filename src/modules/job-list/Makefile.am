AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
	$(FLUX_SECURITY_CFLAGS) \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = libjob-list.la

libjob_list_la_SOURCES = \
	job-list.c \
	job-list.h \
	job_state.h \
	job_state.c \
	job_data.h \
	job_data.c \
	list.h \
	list.c \
	job_util.h \
	job_util.c \
	idsync.h \
	idsync.c \
	stats.h \
	stats.c \
	match.h \
	match.c \
	state_match.h \
	state_match.c \
	match_util.h \
	match_util.c

TESTS = \
	test_job_data.t \
	test_match.t \
	test_state_match.t

test_ldadd = \
	$(builddir)/libjob-list.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(JANSSON_LIBS) \
	$(HWLOC_LIBS)

test_cppflags = \
	$(AM_CPPFLAGS)

test_ldflags = \
	-no-install

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_job_data_t_SOURCES = test/job_data.c
test_job_data_t_CPPFLAGS = \
	-DTEST_SRCDIR=\"$(top_srcdir)/src/modules/job-list/test\" \
	$(test_cppflags)
test_job_data_t_LDADD = \
	$(test_ldadd)
test_job_data_t_LDFLAGS = \
	$(test_ldflags)

test_match_t_SOURCES = test/match.c
test_match_t_CPPFLAGS = \
	$(test_cppflags)
test_match_t_LDADD = \
	$(test_ldadd)
test_match_t_LDFLAGS = \
	$(test_ldflags)

test_state_match_t_SOURCES = test/state_match.c
test_state_match_t_CPPFLAGS = \
	$(test_cppflags)
test_state_match_t_LDADD = \
	$(test_ldadd)
test_state_match_t_LDFLAGS = \
	$(test_ldflags)

EXTRA_DIST = \
	test/R/1node_1core.R \
	test/R/1node_4core.R \
	test/R/4node_1core.R \
	test/R/4node_4core.R \
	test/R/invalid_R_lite.R \
	test/R/invalid_json.R \
	test/R/invalid_nodelist.R \
	test/R/invalid_version.R \
	test/R/missing_R_lite.R \
	test/R/missing_expiration.R \
	test/R/missing_nodelist.R \
	test/R/missing_starttime.R \
	test/R/missing_version.R \
	test/jobspec/1node.jobspec \
	test/jobspec/1node_1slot_nonexclusive.jobspec \
	test/jobspec/1node_perresourcecore4.jobspec \
	test/jobspec/1node_perresourcenode4.jobspec \
	test/jobspec/1slot.jobspec \
	test/jobspec/1slot_4core.jobspec \
	test/jobspec/1slot_perresourcecore4.jobspec \
	test/jobspec/1slot_project_bank.jobspec \
	test/jobspec/4node.jobspec \
	test/jobspec/4node_1slot_nonexclusive.jobspec \
	test/jobspec/4node_4slot_nonexclusive.jobspec \
	test/jobspec/4node_perresourcecore4.jobspec \
	test/jobspec/4node_perresourcenode4.jobspec \
	test/jobspec/4slot.jobspec \
	test/jobspec/4slot_perresourcecore4.jobspec \
	test/jobspec/duration_alt.jobspec \
	test/jobspec/queue_specified.jobspec \
	test/jobspec/cwd_not_specified.jobspec \
	test/jobspec/invalid_attributes_system_job.jobspec \
	test/jobspec/invalid_attributes_system_missing_duration.jobspec \
	test/jobspec/invalid_command_array.jobspec \
	test/jobspec/invalid_command_string.jobspec \
	test/jobspec/invalid_json.jobspec \
	test/jobspec/invalid_per_resource_missing_type.jobspec \
	test/jobspec/invalid_resources.jobspec \
	test/jobspec/invalid_resources_invalid_count.jobspec \
	test/jobspec/invalid_resources_invalid_type.jobspec \
	test/jobspec/invalid_resources_missing_count.jobspec \
	test/jobspec/invalid_resources_missing_type.jobspec \
	test/jobspec/invalid_resources_nocores.jobspec \
	test/jobspec/invalid_resources_noslots.jobspec \
	test/jobspec/invalid_tasks_array.jobspec \
	test/jobspec/invalid_tasks_missing_command.jobspec \
	test/jobspec/invalid_version.jobspec \
	test/jobspec/job_name_alt.jobspec \
	test/jobspec/missing_attributes.jobspec \
	test/jobspec/missing_resources.jobspec \
	test/jobspec/missing_tasks.jobspec \
	test/jobspec/missing_version.jobspec

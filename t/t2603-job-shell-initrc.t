#!/bin/sh
#
test_description='Test flux-shell initrc.lua implementation'

. `dirname $0`/sharness.sh

test_under_flux 4

NCORES=$(flux kvs get resource.R | flux R decode --count=core)
test ${NCORES} -gt 4 && test_set_prereq MULTICORE

FLUX_SHELL="${FLUX_BUILD_DIR}/src/shell/flux-shell"

INITRC_TESTDIR="${SHARNESS_TEST_SRCDIR}/shell/initrc"
INITRC_PLUGINPATH="${SHARNESS_TEST_DIRECTORY}/shell/plugins/.libs"

# test initrc files need to be able to find fluxometer.lua:
export LUA_PATH="${SHARNESS_TEST_DIRECTORY}/?.lua;$(lua -e 'print(package.path)')"

test_expect_success 'flux-shell: initrc: conf.shell_* attributes are set' '
	flux getattr conf.shell_initrc &&
	flux getattr conf.shell_pluginpath
'
test_expect_success 'flux-shell: initrc: conf.shell_initrc can be set' '
	cat <<-EOF >test-initrc.lua &&
	shell.log("loaded test-initrc")
	EOF
	initrc_old=$(flux getattr conf.shell_initrc) &&
	flux setattr conf.shell_initrc $(pwd)/test-initrc.lua &&
	flux run true > test-initrc.output 2>&1 &&
	test_debug "cat test-initrc.output" &&
	grep "loaded test-initrc" test-initrc.output &&
	flux setattr conf.shell_initrc "${initrc_old}"
'
test_expect_success 'flux-shell: initrc: plugin.searchpath set via broker attr' '
	cat <<-EOF >print-searchpath.lua &&
	shell.log("plugin.searchpath = "..plugin.searchpath)
	EOF
	old_pluginpath=$(flux getattr conf.shell_pluginpath) &&
	flux setattr conf.shell_pluginpath /test/foo &&
	flux run -o initrc=$(pwd)/print-searchpath.lua true \
		>print-searchpath.out 2>&1 &&
	test_debug "cat print-searchpath.out" &&
	grep "plugin.searchpath = /test/foo" print-searchpath.out &&
	flux setattr conf.shell_pluginpath "${old_pluginpath}"
'
test_expect_success 'flux-shell: default initrc obeys FLUX_SHELL_RC_PATH' '
	mkdir test-dir.d &&
	cat >test-dir.d/test.lua <<-EOF &&
	shell.log ("plugin loaded from test-dir.d")
	EOF
	FLUX_SHELL_RC_PATH=$(pwd)/test-dir.d \
	  flux run hostname >rcpath.log 2>&1 &&
	grep "plugin loaded from test-dir.d" rcpath.log
'
test_expect_success 'flux-shell: initrc: specifying initrc of /dev/null works' '
	flux run -o verbose -o initrc=/dev/null true > devnull.log 2>&1 &&
	test_debug "cat devnull.log" &&
	grep "Loading /dev/null" devnull.log
'
test_expect_success 'flux-shell: initrc: bad initrc in jobspec fails' '
	test_must_fail_or_be_terminated \
		flux run -o verbose -o initrc=nosuchfile true
'
test_expect_success 'flux-shell: initrc: in jobspec works' '
	name=ok &&
	cat >${name}.lua <<-EOT &&
	    io.stderr:write ("jobspec initrc OK\n")
	EOT
	flux run -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "jobspec initrc OK" ${name}.log
'
test_expect_success 'flux-shell: initrc: failed initrc causes termination' '
	name=failed &&
	cat >${name}.lua <<-EOT &&
	=
	EOT
	test_must_fail_or_be_terminated \
	    flux run -o verbose -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "unexpected symbol" ${name}.log
'
test_expect_success 'flux-shell: initrc: access task in non-task context fails' '
	name=invalid-access &&
	cat >${name}.lua <<-"EOT" &&
	    io.stderr:write (task.info .. "\n")
	EOT
	test_must_fail_or_be_terminated \
	    flux run -o verbose -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "attempt to access task outside of task context" ${name}.log
'
test_expect_success 'flux-shell: initrc: bad plugin.register usage fails' '
	name=bad-register &&
	cat >${name}.lua <<-EOT &&
	    plugin.register {}
	EOT
	test_must_fail_or_be_terminated \
	    flux run -o verbose -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "required handlers table missing" ${name}.log
'
test_expect_success 'flux-shell: initrc: bad plugin.register usage fails' '
	name=handlers-not-array &&
	cat >${name}.lua <<-"EOT" &&
	    plugin.register {
          handlers = {
             topic = "*",
             fn = function (topic) shell.log (topic) end
          }
        }
	EOT
	test_must_fail_or_be_terminated \
	    flux run -o verbose -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "no entries" ${name}.log
'
test_expect_success 'flux-shell: initrc: assignment to shell method fails' '
	name=bad-assign &&
	cat >${name}.lua <<-EOT &&
	    shell.getenv = "my data"
	EOT
	test_must_fail_or_be_terminated \
	    flux run -o verbose -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep " attempt to set read-only field" ${name}.log
'
test_expect_success 'flux-shell: initrc: return false from plugin aborts shell' '
	name=failed-return &&
	cat >${name}.lua <<-EOT &&
	    plugin.register {
              name = "$name",
              handlers = {
		{ topic="*", fn = function () return false end }
              }
	    }
	EOT
	test_must_fail_or_be_terminated \
	    flux run -o verbose -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "plugin.*: shell.init failed" ${name}.log
'
# Submit all test initrcs to instance:
test_expect_success 'flux-shell: initrc: run tests in shell/initrc/tests' '
	flux bulksubmit --quiet --watch -n1 -o initc={} echo run test {./}  \
	    ::: ${INITRC_TESTDIR}/tests/*.lua >&2
'
test_expect_success 'flux-shell: initrc: loads single .so successfully' '
	name=conftest-success &&
	cat >${name}.lua <<-EOT &&
        plugin.searchpath = "${INITRC_PLUGINPATH}"
	plugin.load { file = "dummy.so", conf = { result = 0 } }
	EOT
	flux run -o verbose -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "dummy: OK result=0" ${name}.log
'

test_expect_success 'flux-shell: initrc: script fails on plugin.load failure' '
	name=conftest-failure &&
	cat >${name}.lua <<-EOT &&
        plugin.searchpath = "${INITRC_PLUGINPATH}"
	plugin.load { file = "dummy.so", conf = { result = -1 } }
	EOT
	test_must_fail_or_be_terminated \
	    flux run -o verbose -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "dummy: OK result=-1" ${name}.log
'
test_expect_success 'flux-shell: initrc: plugin pattern nomatch not fatal' '
	name=nomatch-noprob &&
	cat >${name}.lua <<-EOT &&
	plugin.load { file = "nofile*.so" }
	EOT
	flux run --label-io -o verbose -o initrc=${name}.lua \
	    echo Hi > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "0: Hi" ${name}.log
'
test_expect_success 'flux-shell: initrc: plugin nonpattern nomatch fatal' '
	name=nomatch-prob &&
	cat >${name}.lua <<-EOT &&
	plugin.load { file = "nofile.so" }
	EOT
	test_must_fail_or_be_terminated \
	    flux run -o verbose -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "nofile.so: File not found" ${name}.log
'
test_expect_success 'flux-shell: initrc: plugin.load accepts string arg' '
	name=string-arg-nomatch &&
	cat >${name}.lua <<-EOT &&
	plugin.load "nomatch-again.so"
	EOT
	test_must_fail_or_be_terminated \
	    flux run -o verbose -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "nomatch-again.so: File not found" ${name}.log
'
test_expect_success 'flux-shell: initrc: plugin.load bad argument fatal' '
	name=load-bad-arg &&
	cat >${name}.lua <<-EOT &&
	plugin.load (true)
	EOT
	test_must_fail_or_be_terminated \
	    flux run -o verbose -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "plugin.load: invalid argument" ${name}.log
'
test_expect_success 'flux-shell: initrc: plugin.load passes conf to plugin' '
	name=conftest &&
	cat >${name}.lua <<-EOT &&
        plugin.searchpath = "${INITRC_PLUGINPATH}"
	plugin.load { file = "conftest.so",
                      conf = { keys = { "one", "two", "three" },
                               one = "foo",
                               two = "two",
                               three = "bar" }
	            }
	EOT
	flux run -o verbose -o initrc=${name}.lua true > ${name}.log 2>&1 &&
	test_debug "cat ${name}.log" &&
	grep "conftest: one=foo" ${name}.log &&
	grep "conftest: two=two" ${name}.log &&
	grep "conftest: three=bar" ${name}.log
'
test_expect_success 'flux-shell: initrc: load invalid args plugins' '
	name=invalid-args &&
	cat >${name}.lua <<-EOT &&
        plugin.searchpath = "${INITRC_PLUGINPATH}"
	plugin.load { file = "invalid-args.so" }
	EOT
	flux run -o verbose -o initrc=${name}.lua true
'
test_expect_success 'flux-shell: initrc: load getopt plugin' '
	name=getopt &&
	cat >${name}.lua <<-EOF &&
	plugin.searchpath = "${INITRC_PLUGINPATH}"
	plugin.load { file = "getopt.so" }
	EOF
	flux run -o verbose -o initrc=${name}.lua -o test=1 true
'
test_expect_success 'flux-shell: initrc: jobspec-info plugin works' '
	name=jobspec-info &&
	cat >${name}.lua <<-EOF &&
	plugin.searchpath = "${INITRC_PLUGINPATH}"
	plugin.load { file = "jobspec-info.so" }
	EOF
	cat >${name}.json <<-"EOF" &&
	{ "ntasks":1, "nnodes":1, "nslots":1,
	  "cores_per_slot":1,
	  "slots_per_node":1
	}
	EOF
	flux run -N1 -n1 -c1 -o verbose=2 \
		-o initrc=${name}.lua \
		-o ^jobspec_info=${name}.json true
'
test_expect_success 'flux-shell: initrc: shell log functions available' '
	name=shell.log &&
	cat >${name}.lua <<-EOF &&
	shell.log ("test: shell.log")
	shell.debug ("test: debug")

	shell.log_error ("test: error")
	EOF
	flux run -o verbose -o initrc=${name}.lua true \
		> ${name}.log 2>&1 &&
	grep "test: shell.log" ${name}.log &&
	grep "DEBUG: test: debug" ${name}.log &&
	grep "ERROR: test: error" ${name}.log
'
test_expect_success 'flux-shell: initrc: shell.die function works' '
	name=shell.die &&
	cat >${name}.lua <<-EOF &&
        -- shell.die test
	shell.die ("test: shell.die")
	EOF
	test_must_fail_or_be_terminated \
		flux run -o verbose -o initrc=${name}.lua true \
		> ${name}.log 2>&1 &&
	grep "FATAL: test: shell.die" ${name}.log
'
test_expect_success MULTICORE 'flux-shell: initrc: shell.rankinfo reports broker_rank' '
	name=shell.rankinfo.broker_rank &&
	cat >${name}.lua <<-EOF &&
	ri0 = shell.get_rankinfo(0)
	ri1 = shell.get_rankinfo(1)
	if ri0.broker_rank ~= 1 or ri1.broker_rank ~= 3 then
	  shell.log ("rankinfo(0).broker_rank = "
	             ..shell.get_rankinfo(0).broker_rank)
	  shell.log ("rankinfo(1).broker_rank = "
	             ..shell.get_rankinfo(1).broker_rank)
	  shell.die ("rankinfo.broker_rank incorrect!")
	end
	if ri0.ntasks ~= 2 or ri1.ntasks ~= 1 then
	  shell.die ("got ri[0].ntasks = "..ri0.ntasks
	             .." ri[1].ntasks = "..ri1.ntasks)
	end
	EOF
	flux run -N2 -n3 --requires=rank:1,3 \
	    -o verbose -o initrc=${name}.lua true
'
flux job info $(flux job last) R
test_done

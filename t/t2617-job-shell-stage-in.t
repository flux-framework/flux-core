#!/bin/sh
#
test_description='Test flux-shell stage-in plugin support'

. `dirname $0`/sharness.sh

lptest=${FLUX_BUILD_DIR}/t/shell/lptest

test_under_flux 4 job

test_expect_success 'map the red test files' '
	mkdir red &&
	touch red/empty &&
	truncate --size 8192 red/holy &&
	$lptest >red/lptest &&
	echo foo >red/small &&
	mkdir red/dir &&
	ln -s dir red/link &&
	flux filemap map -vv --tags red red
'
test_expect_success 'map the blue test files' '
	dir=blue/a/b/c/d/e/f/g/h &&
	mkdir -p $dir &&
	echo bar >$dir/test &&
	flux filemap map -vv --tags blue blue
'
test_expect_success 'map the main test files' '
	mkdir main &&
	echo "Hello world!" >main/hello &&
	flux filemap map -vv main
'
test_expect_success 'list all the files' '
	flux filemap list --long --tags=red,blue,main
'
test_expect_success 'create file tree checker script' '
	cat >check.sh <<-EOT &&
	#!/bin/sh
	for dir in \$*; do
	    diff -r --no-dereference \$FLUX_JOB_TMPDIR/\$dir \$dir || exit 1
	done
	EOT
	chmod 755 check.sh
'
test_expect_success 'verify that stage-in works with default tag (main)' '
	flux mini run -N4 -ostage-in ./check.sh main
'
test_expect_success 'verify that stage-in works with tags=red' '
	flux mini run -N2 -ostage-in.tags=red ./check.sh red
'
test_expect_success 'verify that stage-in works with tags=red,blue' '
	flux mini run -N1 -ostage-in.tags=red,blue ./check.sh red blue
'
test_expect_success 'verify that stage-in.direct works' '
	flux mini run -N1 \
	    -ostage-in.tags=red \
	    -ostage-in.direct \
	    ./check.sh red
'
test_expect_success 'verify that stage-in.pattern works' '
	flux mini run -N1 \
	    -ostage-in.tags=red,blue \
	    -ostage-in.pattern=red/* \
	    -overbose=2 \
            ./check.sh red 2>pattern.err
'
test_expect_success 'files that did not match pattern were not copied' '
	grep red/small pattern.err &&
	test_must_fail grep blue/a pattern.err
'
test_expect_success 'verify that stage-in.destination works' '
	mkdir testdest &&
	flux mini run -N1 \
	    -o stage-in.destination=$(pwd)/testdest \
	    /bin/true &&
	test -f testdest/main/hello
'
test_expect_success 'verify that stage-in.destination=local:path works' '
	rm -rf testdest/main &&
	flux mini run -N1 \
	    -o stage-in.destination=local:$(pwd)/testdest \
	    /bin/true &&
	test -f testdest/main/hello
'
test_expect_success 'verify that stage-in.destination=global:path works' '
	rm -rf testdest/main &&
	flux mini run -N2 \
	    -o stage-in.destination=global:$(pwd)/testdest \
	    /bin/true &&
	test -f testdest/main/hello
'
test_expect_success 'verify that stage-in.destination fails on bad dir' '
	test_must_fail flux mini run -N1 \
	    -o stage-in.destination=/noexist \
	    /bin/true
'
test_expect_success 'verify that stage-in.destination fails on bad prefix' '
	rm -rf testdest/main &&
	test_must_fail flux mini run -N1 \
	    -o stage-in.destination=wrong:$(pwd)/testdest \
	    /bin/true
'
test_expect_success 'unmap all' '
	flux filemap unmap --tags=red,blue,main
'
test_expect_success 'map a test file and access it to prime the cache' '
	mkdir -p copydir &&
        flux filemap map ./red/lptest &&
	flux filemap get -C copydir &&
	cmp red/lptest copydir/red/lptest
'
test_expect_success 'modify mapped test file without reducing its size' '
        dd if=/dev/zero of=red/lptest bs=4096 count=1 conv=notrunc
'
test_expect_success 'content change should cause an error' '
        test_must_fail flux mini run -N1 -o stage-in /bin/true 2>changed.err &&
	grep changed changed.err
'

test_done

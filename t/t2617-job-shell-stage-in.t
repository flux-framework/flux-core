#!/bin/sh
#
test_description='Test flux-shell stage-in plugin support'

. `dirname $0`/sharness.sh

lptest="flux lptest"

test_under_flux 4 job

test_expect_success 'archive the red test files' '
	mkdir red &&
	touch red/empty &&
	truncate --size 8192 red/holy &&
	$lptest >red/lptest &&
	echo foo >red/small &&
	mkdir red/dir &&
	ln -s dir red/link &&
	flux archive create -v --name=red red
'
test_expect_success 'archive the blue test files' '
	dir=blue/a/b/c/d/e/f/g/h &&
	mkdir -p $dir &&
	echo bar >$dir/test &&
	flux archive create -v --name=blue blue
'
test_expect_success 'archive the main test files' '
	mkdir main &&
	echo "Hello world!" >main/hello &&
	flux archive create -v main
'
test_expect_success 'list all the files' '
	flux archive extract --list-only -v --name=red &&
	flux archive extract --list-only -v --name=blue &&
	flux archive extract --list-only -v
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
test_expect_success 'verify that stage-in works with default key (main)' '
	flux run -N4 -ostage-in ./check.sh main
'
test_expect_success 'verify that stage-in works with names=red' '
	flux run -N2 -ostage-in.names=red ./check.sh red
'
test_expect_success 'verify that stage-in works with names=red,blue' '
	flux run -N1 -ostage-in.names=red,blue ./check.sh red blue
'
test_expect_success 'verify that stage-in works with deprecated tags option' '
	flux run -N1 -ostage-in.tags=red,blue ./check.sh red blue 2>depr.err
'
test_expect_success 'and deprecation warning was printed' '
	grep deprecated depr.err
'
test_expect_success 'verify that stage-in fails with unknown option' '
	test_must_fail flux run -N1 -ostage-in.badopt true
'
test_expect_success 'verify that stage-in fails with unknown archive name' '
	test_must_fail flux run -N1 -ostage-in.names=badarchive true
'
test_expect_success 'verify that stage-in.pattern works' '
	flux run -N1 \
	    -ostage-in.names=red,blue \
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
	flux run -N1 \
	    -o stage-in.destination=$(pwd)/testdest \
	    true &&
	test -f testdest/main/hello
'
test_expect_success 'verify that stage-in.destination=local:path works' '
	rm -rf testdest/main &&
	flux run -N1 \
	    -o stage-in.destination=local:$(pwd)/testdest \
	    true &&
	test -f testdest/main/hello
'
test_expect_success 'verify that stage-in.destination=global:path works' '
	rm -rf testdest/main &&
	flux run -N2 \
	    -o stage-in.destination=global:$(pwd)/testdest \
	    true &&
	test -f testdest/main/hello
'
test_expect_success 'verify that stage-in.destination fails on bad dir' '
	test_must_fail flux run -N1 \
	    -o stage-in.destination=/noexist \
	    true
'
test_expect_success 'verify that stage-in.destination fails on bad prefix' '
	rm -rf testdest/main &&
	test_must_fail flux run -N1 \
	    -o stage-in.destination=wrong:$(pwd)/testdest \
	    true
'
test_expect_success 'remove archives' '
	flux archive remove &&
	flux archive remove --name=blue &&
	flux archive remove --name=red
'
test_expect_success 'create a test file with random content' '
        dd if=/dev/urandom of=foo bs=4096 count=1 conv=notrunc
'
test_expect_success 'map test file and access it to prime the cache' '
	mkdir -p copydir &&
        flux archive create --mmap ./foo &&
	flux archive extract -C copydir &&
	cmp foo copydir/foo
'
test_expect_success 'modify mapped test file without reducing its size' '
        dd if=/dev/zero of=foo bs=4096 count=1 conv=notrunc
'
test_expect_success 'content change should cause an error' '
        test_must_fail flux run -N1 -o stage-in true 2>changed.err &&
	grep changed changed.err
'
test_expect_success 'Create files for example 2 of flux-archive(1)' '
	mkdir -p project/dataset1 &&
	mkdir -p home/fred &&
	$lptest >project/dataset1/lptest &&
	cat >home/fred/app <<-EOT &&
	#!/bin/sh
	find \$1 -type f
	EOT
	chmod +x home/fred/app
'
test_expect_success 'Verify that example 2 of flux-archive(1) works' '
	flux archive create --name=dataset1 -C project dataset1 &&
	flux archive create --name=app --mmap -C home/fred app &&
	flux run -N4 -o stage-in.names=app,dataset1 \
	    {{tmpdir}}/app {{tmpdir}}/dataset1 &&
	flux archive remove --name=dataset1 &&
	flux archive remove --name=app
'

test_done

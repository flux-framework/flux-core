#!/bin/sh

test_description='Test content ENOSPC corner cases'

. `dirname $0`/sharness.sh

if ! ls /test/tmpfs-1m || ! ls /test/tmpfs-5m; then
	skip_all='skipping ENOSPC tests, no small tmpfs mounted'
	test_done
fi

test_expect_success 'create script to fill statedir via jobs' '
	cat >fillstatedir.sh <<-EOT &&
	#!/bin/sh
	while true ; do
	      flux run echo 0123456789 > /dev/null 2>&1
	      if flux dmesg | grep -q "No space left on device"; then
		   break
	      fi
	done
	EOT
	chmod +x fillstatedir.sh
'

test_expect_success 'create script to fill dir with junk' '
	cat >filljunk.sh <<-EOT &&
	#!/bin/sh
	i=0
	dir=\$1
	while dd if=/dev/zero of=\${dir}/junk\${i}.out bs=100k count=1 > /dev/null 2>&1 ; do
		i=\$((i+1))
	done
	EOT
	chmod +x filljunk.sh
'

# flux start will fail b/c rc3 will fail due to ENOSPC
test_expect_success 'flux still operates with content-sqlite running out of space' '
	rm -rf /test/tmpfs-1m/* &&
	mkdir /test/tmpfs-1m/statedir &&
	test_must_fail flux start \
	    -o,-Scontent.backing-module=content-sqlite \
	    -o,-Sstatedir=/test/tmpfs-1m/statedir \
	    "./fillstatedir.sh; flux dmesg; flux run echo helloworld" > sql.out 2> sql.err &&
	grep -q "No space left on device" sql.out &&
	grep "helloworld" sql.out
'

# flux start will fail b/c rc3 will fail due to ENOSPC
test_expect_success 'flux still operates with content-files running out of space' '
	rm -rf /test/tmpfs-1m/* &&
	mkdir /test/tmpfs-1m/statedir &&
	test_must_fail flux start \
	    -o,-Scontent.backing-module=content-files \
	    -o,-Sstatedir=/test/tmpfs-1m/statedir \
	    "./fillstatedir.sh; flux dmesg; flux run echo helloworld" > files.out 2> files.err &&
	grep -q "No space left on device" files.out &&
	grep "helloworld" files.out
'

# flux start will fail b/c rc3 will fail due to ENOSPC
test_expect_success 'content flush returns error on ENOSPC' '
	rm -rf /test/tmpfs-1m/* &&
	mkdir /test/tmpfs-1m/statedir &&
	test_must_fail flux start \
	    -o,-Scontent.backing-module=content-sqlite \
	    -o,-Sstatedir=/test/tmpfs-1m/statedir \
	    "./fillstatedir.sh; flux dmesg; flux content flush" > flush.out 2> flush.err &&
	grep -q "No space left on device" flush.out &&
	grep "content.flush: No space left on device" flush.err
'

test_expect_success 'preallocate fails on excess amount' '
	rm -rf /test/tmpfs-5m/* &&
	mkdir /test/tmpfs-5m/statedir &&
	flux start -o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		-o,-Sstatedir=/test/tmpfs-5m/statedir \
		"flux module load content; \
		flux module load content-sqlite preallocate=10000000; \
		flux module remove content" > preallocatefail.out 2> preallocatefail.err &&
	grep "preallocate" preallocatefail.err | grep "database or disk is full"
'

# flux start will fail b/c rc3 will fail due to ENOSPC
test_expect_success 'test fill up dir with junk script works' '
	rm -rf /test/tmpfs-5m/* &&
	mkdir /test/tmpfs-5m/statedir &&
	test_must_fail flux start \
	    -o,-Scontent.backing-module=content-sqlite \
	    -o,-Sstatedir=/test/tmpfs-5m/statedir \
	    "./filljunk.sh /test/tmpfs-5m/statedir; flux run echo helloworld" > filljunk.out 2> filljunk.err &&
        ls /test/tmpfs-5m/statedir | grep -q junk &&
        grep -q "No space left on device" filljunk.err &&
        grep "helloworld" filljunk.out
'

# In 5m tmpfs, we preallocate 3m and fill up rest of space with junk
test_expect_success 'preallocate works if we dont use journaling' '
	cat >content-sqlite.toml <<-EOT &&
	[content-sqlite]
	journal_mode = "OFF"
	synchronous = "OFF"
	preallocate = 3145728
	EOT
	rm -rf /test/tmpfs-5m/* &&
	mkdir /test/tmpfs-5m/statedir &&
	flux start \
	    -o,-Scontent.backing-module=content-sqlite \
	    -o,-Sstatedir=/test/tmpfs-5m/statedir \
	    -o,--config-path=$(pwd) \
	    "./filljunk.sh /test/tmpfs-5m/statedir; flux run echo helloworld" > prealloc1.out 2> prealloc1.err &&
	ls /test/tmpfs-5m/statedir | grep -q junk &&
	test_must_fail grep "No space left on device" prealloc1.err &&
	grep "helloworld" prealloc1.out &&
	rm content-sqlite.toml
'

# In 5m tmpfs, we preallocate 3m and fill up rest of space with junk
# flux start will fail b/c rc3 will fail due to ENOSPC
test_expect_success 'preallocate will still hit ENOSPC at some point' '
	cat >content-sqlite.toml <<-EOT &&
	[content-sqlite]
	journal_mode = "OFF"
	synchronous = "OFF"
	preallocate = 2097152
	EOT
	rm -rf /test/tmpfs-5m/* &&
	mkdir /test/tmpfs-5m/statedir &&
	test_must_fail flux start \
	    -o,-Scontent.backing-module=content-sqlite \
	    -o,-Sstatedir=/test/tmpfs-5m/statedir \
	    -o,--config-path=$(pwd) \
	    "./filljunk.sh /test/tmpfs-5m/statedir; ./fillstatedir.sh; flux dmesg; flux run echo helloworld" \
	    > prealloc2.out 2> prealloc2.err &&
	ls /test/tmpfs-5m/statedir | grep -q junk &&
	grep "No space left on device" prealloc2.out &&
	grep "helloworld" prealloc2.out &&
	rm content-sqlite.toml
'

test_expect_success 'test preallocate works w/ journaling' '
	cat >content-sqlite.toml <<-EOT &&
	[content-sqlite]
	preallocate = 3145728
	EOT
	rm -rf /test/tmpfs-5m/* &&
	mkdir /test/tmpfs-5m/statedir &&
	flux start \
	    -o,-Scontent.backing-module=content-sqlite \
	    -o,-Sstatedir=/test/tmpfs-5m/statedir \
	    -o,--config-path=$(pwd) \
	    "./filljunk.sh /test/tmpfs-5m/statedir; flux run echo helloworld; flux dmesg" \
	    > prealloc3.out 2> prealloc3.err &&
        ls /test/tmpfs-5m/statedir | grep -q junk &&
        test_must_fail grep "No space left on device" prealloc3.err &&
        grep "preallocate reconfig" prealloc3.out | grep "journal_mode=OFF" &&
        grep "preallocate reconfig" prealloc3.out | grep "synchronous=FULL" &&
        grep "helloworld" prealloc3.out &&
	rm content-sqlite.toml
'

test_done

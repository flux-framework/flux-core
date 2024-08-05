test_expect_success_hd() {
	test "$#" = 2 && { test_prereq=$1; shift; } || test_prereq=
	local TEST_CODE
	# these extra newlines are intentional, and mimic the ones we get
	# naturally in the non-heredoc case
	TEST_CODE="\

	$(cat)
	"
	test_expect_success "$test_prereq" "'$1'" "$TEST_CODE"
}

test_expect_failure_hd() {
	test "$#" = 2 && { test_prereq=$1; shift; } || test_prereq=
	local TEST_CODE
	# these extra newlines are intentional, and mimic the ones we get
	# naturally in the non-heredoc case
	TEST_CODE="\

	$(cat)
	"
	test_expect_failure "$test_prereq" "'$1'" "$TEST_CODE"
}


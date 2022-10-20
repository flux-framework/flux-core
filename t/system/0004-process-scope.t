#
#  Process scope test
#
test_expect_success 'scope returns system instance' '
	flux scope > scope.out &&
	echo "system instance" > scope.exp &&
	test_cmp scope.exp scope.out
'

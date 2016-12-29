
SUBDIRS=flux

EXTRA_DIST = pycotap test test_commands make_binding.py .pylintrc

clean-local:
	-rm -f test/*.pyc test_commands/*.pyc
	-rm -f .coverage*

dist-hook:
	-rm -f test/*.pyc test_commands/*.pyc
	-rm -f test_commands/*.log test_commands/*.trs

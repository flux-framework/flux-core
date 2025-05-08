# Flux Core Documentation

Flux-core documentation currently consists of man pages written in ReStructured Text (rst). The man pages are generated using sphinx and are also hosted at flux-framework.readthedocs.io.

##  Build Instructions

To get setup with a virtual environment run:

```bash
virtualenv -p python3 sphinx-rtd
source sphinx-rtd/bin/activate
git clone git@github.com:flux-framework/flux-core
cd flux-core/doc
pip install -r requirements.txt
```

Users can then build the documentation, either as man pages or html web pages.
Users can also run spellcheck.

```
sphinx-build -M man ./ _build/
sphinx-build -M html ./ _build/
sphinx-build -W -b spelling ./ _build/
```

## Adding a New Man Page

There are 2 steps to adding a man page:
- creating the man page documentation file
- configuring the generation of the man page(s)

### Creating the Man Page

Man pages are written as [ReStructured Text](https://www.sphinx-doc.org/en/master/usage/restructuredtext/basics.html) (`.rst`) files.
We use [Sphinx](https://www.sphinx-doc.org/en/master/) to process the documentation files and turn them into man pages (troff) and web pages (html).

Sphinx automatically adds the following sections to the generated man page (so do not include them in the `.rst` file):

- `NAME` (first section)
- `AUTHOR` (penultimate section)
- `COPYRIGHT` (final section)

Each section title should be underlined with `=`

### Configuring Generation

Generating a man pages is done via the `man_pages` variable in `conf.py`. For example:

```
man_pages = [
    ('man1/flux', 'flux', 'the Flux resource management framework', [author], 1),
]
```

The tuple entry in the `man_pages` list specifies:
- File name (relative path, without the `.rst` extension)
- Name of man page
- Description of man page
- Author (use `[author]` as in the example)
- Manual section for the generated man page

It is possible for multiple man pages to be generated from a single source file.
Simply create an entry for each man page you want generated.
These entries can have the same file path, but different man page names.

### A Note on Sphinx Errors
Sphinx writes helpful error logs to `TMPDIR`, so when debugging, if you're looking for a more useful error message than 
```
make: *** [Makefile:1731: html] Error 2
```
look inside your `TMPDIR` and you will see a useful log file.
```
$ ls /tmp/elvis/
sphinx-err-syr84dmk.log
$ 
$ cat /tmp/elvis/sphinx-err-syr84dmk.log
# Sphinx version: 5.3.0
# Python version: 3.6.8 (CPython)
# Docutils version: 0.17.1 release
# Jinja2 version: 3.0.3
# Last messages:

# Loaded extensions:
Traceback (most recent call last):
  File "/g/g0/elvis/k8senv/lib64/python3.6/site-packages/sphinx/cmd/build.py", line 280, in build_main
    args.pdb)
  File "/g/g0/elvis/k8senv/lib64/python3.6/site-packages/sphinx/application.py", line 237, in __init__
    self.config.setup(self)
  File "/g/g0/elvis/flux-core/doc/conf.py", line 156, in setup
    app.connect('builder-inited', run_apidoc)
NameError: name 'run_apidoc' is not defined
```


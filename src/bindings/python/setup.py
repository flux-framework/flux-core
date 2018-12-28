from setuptools import setup
import os

here = os.path.abspath(os.path.dirname(__file__))
cffi_dep = "cffi>=1.1"
setup(
    name="flux",
    version="0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.1a1",
    description="Bindings to the flux resource manager API",
    setup_requires=[cffi_dep],
    cffi_modules=[
        "_flux/_core_build.py:ffi",
        "_flux/_jsc_build.py:ffi",
        "_flux/_kvs_build.py:ffi",
        "_flux/_kz_build.py:ffi",
    ],
    install_requires=[cffi_dep],
)

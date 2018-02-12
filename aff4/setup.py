#!/usr/bin/env python

"""
setup.py file for libaff4 SWIG module
"""

from distutils.core import setup, Extension

libaff4 = Extension('_libaff4', sources=[
    'libaff4_wrap.c'
])

setup(
    name='libaff4',
    version='0.1',
    author='scudette@gmail.com',
    description="""AFF4 interface library.""",
    ext_modules=[
        libaff4
    ],
    py_modules=[
        "libaff4"
    ],
)

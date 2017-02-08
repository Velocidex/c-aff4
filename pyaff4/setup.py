# Copyright 2014 Google Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.

"""This module installs the pyaff4 library."""

from setuptools import setup
from setuptools.command.test import test as TestCommand

try:
    with open('../README.md') as file:
        long_description = file.read()
except IOError:
    long_description = ""

ENV = {"__file__": __file__}
exec open("pyaff4/_version.py").read() in ENV
VERSION = ENV["get_versions"]()


class NoseTestCommand(TestCommand):
    def finalize_options(self):
        TestCommand.finalize_options(self)
        self.test_args = []
        self.test_suite = True

    def run_tests(self):
        # Run nose ensuring that argv simulates running nosetests directly
        import nose
        nose.run_exit(argv=['nosetests'])


commands = {}
commands["test"] = NoseTestCommand

setup(
    name='pyaff4',
    long_description=long_description,
    version=VERSION["pep440"],
    cmdclass=commands,
    description='Python Advanced Forensic Format Version 4 library.',
    author='Michael Cohen',
    author_email='scudette@gmail.com',
    url='https://www.aff4.org/',
    packages=['pyaff4'],
    package_dir={"pyaff4": "pyaff4"},
    install_requires=[
        "aff4-snappy == 0.5",
        "rdflib == 4.2.1",
        "intervaltree == 2.1.0",
    ],
    extras_require=dict(
        cloud="google-api-python-client"
    )
)

#!/bin/python3

import os
import subprocess

# Prepares this directory for running the integration tests.
if not os.access("ReferenceImages", os.O_RDONLY):
    subprocess.check_call([
        "git", "clone", "https://github.com/aff4/ReferenceImages.git"])

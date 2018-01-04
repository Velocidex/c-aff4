#!/bin/python3

import os
import subprocess


def prepare():
    """Prepares this directory for running the integration tests."""
    if not os.access("ReferenceImages", os.O_RDONLY):
        subprocess.check_call([
            "git", "clone", "https://github.com/aff4/ReferenceImages.git"])

    # Create a large file for testing. The file needs to be larger
    # than 4Gb to test Zip64's handling of large volumes.
    if not os.access("LargeFile.raw", os.O_RDONLY):
        # 1Mb of random data.
        data = os.urandom(1*1024*1024)
        with open("LargeFile.raw", "wb") as fd:
            while fd.tell() < 4.2E9:
                fd.write(data)


if __name__ == "__main__":
    prepare()

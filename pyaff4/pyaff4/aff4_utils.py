from __future__ import unicode_literals
from future import standard_library
standard_library.install_aliases()
from builtins import chr
import os
import re
import shutil
import string
import urllib.parse

from pyaff4 import rdfvalue
from pyaff4 import utils


PRINTABLES = set(string.printable)
for i in "!$\\:*%?\"<>|]":
    PRINTABLES.discard(i)

PRINTABLES_NO_SLASH = PRINTABLES.copy()
PRINTABLES_NO_SLASH.discard('/')


def member_name_for_urn(member_urn, base_urn=None, slash_ok=True):
    if slash_ok:
        acceptable_set = PRINTABLES
    else:
        acceptable_set = PRINTABLES_NO_SLASH

    filename = base_urn.RelativePath(member_urn)
    # The member is not related to the base URN, just concatenate them together.
    if filename is None:
        filename = os.path.join(
            base_urn.Parse().path, member_urn.SerializeToString())

    if filename.startswith("/"):
        filename = filename[1:]

    # original implementations of AFF4 (and Evimetry) escape the leading aff4://
    if filename.startswith("aff4://"):
        return filename.replace("aff4://", "aff4%3A%2F%2F")

    # Escape chars which are non printable.
    escaped_filename = []
    for c in filename:
        if c in acceptable_set:
            escaped_filename.append(c)
        else:
            escaped_filename.append("%%%02x" % ord(c))

    return "".join(escaped_filename)


def urn_from_member_name(member, base_urn):
    """Returns a URN object from a zip file's member name."""
    member = utils.SmartUnicode(member)

    # Remove %xx escapes.
    member = re.sub(
        "%(..)", lambda x: chr(int("0x" + x.group(1), 0)),
        member)

    # This is an absolute URN.
    if urllib.parse.urlparse(member).scheme == "aff4":
        result = member
    else:
        # Relative member becomes relative to the volume's URN.
        result = base_urn.Append(member, quote=False)

    return rdfvalue.URN(result)


def MkDir(path):
    try:
        os.mkdir(path)
    except OSError as e:
        if "File exists" in e.strerror:
            return

        raise

def RemoveDirectory(path):
    try:
        shutil.rmtree(path)
    except OSError:
        pass

def EnsureDirectoryExists(path):
    dirname = os.path.dirname(path)
    try:
        os.makedirs(dirname)
    except OSError:
        pass

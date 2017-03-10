import os
import re
import shutil
import string
import urlparse


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

    # Escape chars which are non printable.
    escaped_filename = []
    for c in filename:
        if c in acceptable_set:
            escaped_filename.append(c)
        else:
            escaped_filename.append("%%%02x" % ord(c))

    return "".join(escaped_filename)

def urn_from_member_name(member, base_urn):
    # Remove %xx escapes.
    member = re.sub(
        "%(..)", lambda x: chr(int("0x" + x.group(1), 0)),
        member)
    if urlparse.urlparse(member).scheme == "aff4":
        return member

    return base_urn.Append(member, quote=False)

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

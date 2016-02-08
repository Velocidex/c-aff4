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

"""This is the python AFF4 library."""
import platform
import sys
import time
import uuid
import weakref

from pyaff4 import rdfvalue


class NoneObject(object):
    """ A magical object which is like None but swallows bad
    dereferences, __getattr__, iterators etc to return itself.

    Instantiate with the reason for the error.
    """

    def __init__(self, reason="", *args, **_):
        # Often None objects are instantiated on purpose so its not really that
        # important to see their reason.
        self.reason = reason
        self.args = args

    def __str__(self):
        return unicode(self).encode('utf-8')

    def __unicode__(self):
        return self.FormatReason()

    def FormatReason(self):
        if "%" in self.reason:
            return self.reason % self.args
        else:
            return self.reason.format(*self.args)

    def __repr__(self):
        return "<%s>" % self.FormatReason()

    ## Behave like an empty set
    def __iter__(self):
        return iter([])

    def __len__(self):
        return 0

    def __getattr__(self, attr):
        # By returning self for any unknown attribute and ensuring the self is
        # callable, we cover both properties and methods Override NotImplemented
        # functions in object with self
        return self

    def __bool__(self):
        return False

    def __nonzero__(self):
        return False

    # Comparisons.
    def __eq__(self, other):
        return other is None

    def __ne__(self, other):
        return other is not None

    def __gt__(self, _):
        return False

    __lt__ = __gt__
    __le__ = __gt__
    __ge__ = __gt__

    ## Make us subscriptable obj[j]
    def __getitem__(self, item):
        return self

    def __call__(self, *arg, **kwargs):
        return self

    def __int__(self):
        return -1

    __add__ = __call__
    __sub__ = __call__
    __mul__ = __call__
    __floordiv__ = __call__
    __mod__ = __call__
    __div__ = __call__
    __divmod__ = __call__
    __pow__ = __call__
    __lshift__ = __call__
    __rshift__ = __call__
    __and__ = __call__
    __xor__ = __call__
    __or__ = __call__

    __radd__ = __call__
    __rsub__ = __call__
    __rmul__ = __call__
    __rfloordiv__ = __call__
    __rmod__ = __call__
    __rdivmod__ = __call__
    __rpow__ = __call__
    __rlshift__ = __call__
    __rrshift__ = __call__
    __rand__ = __call__
    __rxor__ = __call__
    __ror__ = __call__

    # Override these methods too.
    dereference_as = __call__
    __getitem__ = __call__

    def __enter__(self):
        return self
    def __exit__(self, exc_type, exc_value, traceback):
        # Do not allow exceptions to be propagated through us.
        return True


# Keep track of all the AFF4 objects which are alive right now. This helps in
# debugging memory leaks.
AFF4_OBJECT_REFS = {}


class AFF4StreamProperties(object):
    seekable = True
    sizeable = True
    writable = False

class AFF4VolumeProperties(object):
    supports_compression = True
    writable = False
    files_are_directories = True


class AFF4Object(object):
    def __init__(self, resolver, urn=None):
        self.resolver = resolver
        self._dirty = False
        if urn is None:
            urn = "aff4://%s" % uuid.uuid4()

        self.urn = rdfvalue.URN(urn)
        AFF4_OBJECT_REFS[id(self)] = weakref.proxy(
            self, lambda _, id=id(self), ref=AFF4_OBJECT_REFS: ref.pop(id))

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        # Return ourselves to the resolver cache.
        self.resolver.Return(self)

    def LoadFromURN(self):
        raise NotImplementedError

    def Prepare(self):
        pass

    def Flush(self):
        self._dirty = False

    def IsDirty(self):
        return self._dirty

    def MarkDirty(self):
        self._dirty = True


class AFF4Volume(AFF4Object):
    def __init__(self, *args, **kwargs):
        super(AFF4Volume, self).__init__(*args, **kwargs)
        self.properties = AFF4VolumeProperties()

    def CreateMember(self, child):
        raise NotImplementedError


SEEK_SET = 0
SEEK_CUR = 1
SEEK_END = 2


class AFF4Stream(AFF4Object):
    readptr = 0
    size = 0

    def __init__(self, *args, **kwargs):
        super(AFF4Stream, self).__init__(*args, **kwargs)
        self.properties = AFF4StreamProperties()

    def Read(self, length):
        raise NotImplementedError()

    def Write(self, data):
        raise NotImplementedError()

    def WriteStream(self, source):
        """Writes into this stream from a stream.

        The stream is a file-like object with read and tell() methods.
        """
        raise NotImplementedError()

    def Seek(self, offset, whence=0):
        if whence == SEEK_SET:
            self.readptr = offset
        elif whence == SEEK_CUR:
            self.readptr += offset
        elif whence == SEEK_END:
            self.readptr = offset + self.Size()

        if self.readptr < 0:
            self.readptr = 0

    def Tell(self):
        return self.readptr

    def Size(self):
        return self.size

    def read(self, length=1024*1024):
        return self.Read(length)

    def seek(self, offset, whence=0):
        self.Seek(offset, whence=whence)

    def write(self, data):
        self.Write(data)

    def tell(self):
        return self.Tell()

    def flush(self):
        self.Flush()

    def Prepare(self):
        self.Seek(0)


class ProgressContext(object):
    last_time = 0
    last_offset = 0

    # The following are set in advance by users in order to get accurate
    # progress reports.

    # Start offset of this current range.
    start = 0
    length = 0

    def __init__(self, length=0):
        self.length = length
        self.last_time = self.now()

    def now(self):
        return time.time() * 1e6

    def Report(self, readptr):
        """This will be called periodically to report the progress.

        Note that readptr is specified relative to the start of the range
        operation (WriteStream and CopyToStream)
        """
        readptr = readptr + self.start
        now = self.now()
        if now > self.last_time + 1000000/4:
            # Rate in MB/s.
            rate = ((readptr - self.last_offset) /
                    (now - self.last_time) * 1000000 / 1024/1024)

            sys.stdout.write(" Reading %sMiB / %sMiB  %s MiB/s\r\n" % (
                readptr/1024/1024,
                self.length/1024/1024,
                rate))
            sys.stdout.flush()

            self.last_time = now
            self.last_offset = readptr

        if aff4_abort_signaled:
            sys.stdout.write("\n\nAborted!\n")
            raise RuntimeError("Aborted")

aff4_abort_signaled = False


class EmptyProgressContext(ProgressContext):
    def Report(self, _):
        pass

DEFAULT_PROGRESS = ProgressContext()
EMPTY_PROGRESS = EmptyProgressContext()


WIN32 = platform.system() == "Windows"

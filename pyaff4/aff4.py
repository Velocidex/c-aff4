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
import uuid
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



class AFF4Object(object):
    def __init__(self, resolver, urn=None):
        self.resolver = resolver
        self._dirty = False
        if urn is None:
            urn = "aff4://%s" % uuid.uuid4()

        self.urn = rdfvalue.URN(urn)

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
    def CreateMember(self, child):
        raise NotImplementedError


class AFF4Stream(AFF4Object):
    readptr = 0
    size = 0

    def Read(self, length):
        raise NotImplementedError

    def Write(self, data):
        raise NotImplementedError

    def Seek(self, offset, whence=0):
        if whence == 0:
            self.readptr = offset
        elif whence == 1:
            self.readptr += offset
        elif whence == 2:
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

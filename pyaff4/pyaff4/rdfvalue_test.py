from __future__ import unicode_literals
from pyaff4 import rdfvalue
import unittest

class URNTest(unittest.TestCase):

    def testXSDInt(self):
        i1 = rdfvalue.XSDInteger("100")
        self.assertLess(99, i1)
        self.assertEqual(100, i1)
        self.assertGreater(101, 100)
        self.assertGreater(101, i1)
        self.assertTrue(99 < i1)
        self.assertTrue(101 > i1)
        self.assertTrue(100 == i1)

    def testURN(self):
        url = "http://www.google.com/path/to/element#hash_data"
        self.assertEquals(rdfvalue.URN(url), url)
        self.assertEquals(rdfvalue.URN("//etc/passwd"),
                          "file://etc/passwd")

    def testTrailingSlashURN(self):
        url = "http://code.google.com/p/snappy/"
        test = rdfvalue.URN(url)
        self.assertEquals(test.SerializeToString(),
                          b"http://code.google.com/p/snappy/")

    def testAppend(self):
        test = rdfvalue.URN("http://www.google.com")

        self.assertEquals(test.Append("foobar").SerializeToString(),
                          b"http://www.google.com/foobar")

        self.assertEquals(test.Append("/foobar").SerializeToString(),
                          b"http://www.google.com/foobar")

        self.assertEquals(test.Append("..").SerializeToString(),
                          b"http://www.google.com/")

        self.assertEquals(test.Append("../../../..").SerializeToString(),
                          b"http://www.google.com/")

        self.assertEquals(test.Append("aa/bb/../..").SerializeToString(),
                          b"http://www.google.com/")

        self.assertEquals(test.Append("aa//../c").SerializeToString(),
                          b"http://www.google.com/c")

        self.assertEquals(
            test.Append("aa///////////.///./c").SerializeToString(),
            b"http://www.google.com/aa/c")


if __name__ == '__main__':
    unittest.main()

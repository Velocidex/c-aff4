import rdfvalue
import unittest

class URNTest(unittest.TestCase):
    def testURN(self):
        url = "http://www.google.com/path/to/element#hash_data"
        self.assertEquals(rdfvalue.URN(url).SerializeToString(), url)
        self.assertEquals(rdfvalue.URN("//etc/passwd").SerializeToString(),
                          "file://etc/passwd")

    def testAppend(self):
        test = rdfvalue.URN("http://www.google.com")

        self.assertEquals(test.Append("foobar").SerializeToString(),
                          "http://www.google.com/foobar")

        self.assertEquals(test.Append("/foobar").SerializeToString(),
                          "http://www.google.com/foobar")

        self.assertEquals(test.Append("..").SerializeToString(),
                          "http://www.google.com/")

        self.assertEquals(test.Append("../../../..").SerializeToString(),
                          "http://www.google.com/")

        self.assertEquals(test.Append("aa/bb/../..").SerializeToString(),
                          "http://www.google.com/")

        self.assertEquals(test.Append("aa//../c").SerializeToString(),
                          "http://www.google.com/c")

        self.assertEquals(
            test.Append("aa///////////.///./c").SerializeToString(),
            "http://www.google.com/aa/c")


if __name__ == '__main__':
    unittest.main()

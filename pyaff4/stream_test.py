import data_store
import os
import lexicon
import rdfvalue
import unittest
import plugins


class StreamTest(unittest.TestCase):
    def streamTest(self, stream):
        self.assertEquals(0, stream.Tell())
        self.assertEquals(0, stream.Size())

        stream.Write("hello world")
        self.assertEquals(11, stream.Tell())

        stream.Seek(0, 0)
        self.assertEquals(0, stream.Tell())

        self.assertEquals("hello world",
                          stream.Read(1000))

        self.assertEquals(11, stream.Tell())

        stream.Seek(-5, 2)
        self.assertEquals(6, stream.Tell())

        self.assertEquals("world",
                          stream.Read(1000))

        stream.Seek(-5, 2)
        self.assertEquals(6, stream.Tell())

        stream.Write("Cruel world")
        stream.Seek(0, 0)
        self.assertEquals(0, stream.Tell())
        self.assertEquals("hello Cruel world",
                          stream.Read(1000))

        self.assertEquals(17, stream.Tell())

        stream.Seek(0, 0)

        self.assertEquals("he",
                          stream.Read(2))

        stream.Write("I have %d arms and %#x legs." % (2, 1025))
        self.assertEquals(31, stream.Tell())

        stream.Seek(0, 0)
        self.assertEquals("heI have 2 arms and 0x401 legs.",
                          stream.Read(1000))

    def testFileBackedStream(self):
        filename = rdfvalue.URN("/tmp/test_filename.bin")
        resolver = data_store.MemoryDataStore()
        try:
            resolver.Set(filename, lexicon.AFF4_STREAM_WRITE_MODE,
                         rdfvalue.XSDString("truncate"))

            file_stream = resolver.AFF4FactoryOpen(filename)
            self.streamTest(file_stream)
        finally:
            os.unlink(filename.Parse().path)


if __name__ == '__main__':
    unittest.main()

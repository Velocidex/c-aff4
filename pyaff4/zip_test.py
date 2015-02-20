import data_store
import os
import lexicon
import rdfvalue
import unittest
import plugins
import zip

class ZipTest(unittest.TestCase):
    filename = "/tmp/aff4_test.zip"
    segment_name = "Foobar.txt"
    data1 = "I am a segment!"
    data2 = "I am another segment!"

    def setUp(self):
        with data_store.MemoryDataStore() as resolver:
            resolver.Set(self.filename, lexicon.AFF4_STREAM_WRITE_MODE,
                         rdfvalue.XSDString("truncate"))

            with zip.ZipFile.NewZipFile(resolver, self.filename) as zip_file:
                self.volume_urn = zip_file.urn
                segment_urn = self.volume_urn.Append(self.segment_name)

                with zip_file.CreateMember(segment_urn) as segment:
                    segment.Write(self.data1)

                with zip_file.CreateMember(segment_urn) as segment2:
                    segment2.Seek(0, 2)
                    segment2.Write(self.data2)

    def tearDown(self):
        try:
            os.unlink(self.filename)
        except (IOError, OSError):
            pass

    def testOpenSegmentByURN(self):
        resolver = data_store.MemoryDataStore()

        # This is required in order to load and parse metadata from this volume
        # into a fresh empty resolver.
        with zip.ZipFile.NewZipFile(resolver, self.filename) as zip_file:
            segment_urn = zip_file.urn.Append(self.segment_name)

        with resolver.AFF4FactoryOpen(segment_urn) as segment:
            self.assertEquals(segment.Read(1000), self.data1 + self.data2)

if __name__ == '__main__':
    unittest.main()

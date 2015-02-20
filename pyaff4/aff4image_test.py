import aff4_image
import data_store
import logging
import os
import lexicon
import rdfvalue
import zip
import unittest
import plugins

class AFF4ImageTest(unittest.TestCase):
    filename = "/tmp/aff4_test.zip"
    image_name = "image.dd"

    def tearDown(self):
        try:
            os.unlink(self.filename)
        except (IOError, OSError):
            pass

    def setUp(self):
        with data_store.MemoryDataStore() as resolver:
            resolver.Set(self.filename, lexicon.AFF4_STREAM_WRITE_MODE,
                         rdfvalue.XSDString("truncate"))

            with zip.ZipFile.NewZipFile(resolver, self.filename) as zip_file:
                self.volume_urn = zip_file.urn
                image_urn = self.volume_urn.Append(self.image_name)

                with aff4_image.AFF4Image.NewAFF4Image(
                    resolver, image_urn, self.volume_urn) as image:
                    image.chunk_size = 10
                    image.chunks_per_segment = 3

                    for i in range(100):
                        image.Write("Hello world %02d!" % i)
                    self.image_urn = image.urn

    def testOpenImageByURN(self):
        resolver = data_store.MemoryDataStore()

        # This is required in order to load and parse metadata from this volume
        # into a fresh empty resolver.
        with zip.ZipFile.NewZipFile(resolver, self.filename) as zip_file:
            image_urn = zip_file.urn.Append(self.image_name)

        with resolver.AFF4FactoryOpen(image_urn) as image:
            self.assertEquals(image.chunk_size, 10)
            self.assertEquals(image.chunks_per_segment, 3)
            self.assertEquals(
                "Hello world 00!Hello world 01!Hello world 02!Hello world 03!"
                "Hello world 04!Hello world 05!Hello worl",
                image.Read(100))

            self.assertEquals(1500, image.Size())


if __name__ == '__main__':
    #logging.getLogger().setLevel(logging.DEBUG)
    unittest.main()

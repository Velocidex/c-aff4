from __future__ import print_function
from __future__ import unicode_literals
import os
import unittest

from pyaff4 import container
from pyaff4 import hashing_test


class ContainerTest(unittest.TestCase):

    @hashing_test.conditional_on_images
    def testOpen(self):
        fd = container.Container.open(hashing_test.stdLinear)
        self.assertEqual(fd.urn,
                         u"aff4://fcbfdce7-4488-4677-abf6-08bc931e195b")


if __name__ == '__main__':
    unittest.main()

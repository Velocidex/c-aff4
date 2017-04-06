#!/bin/bash

# extract images
../src/aff4imager --verbose --export aff4://cf853d0b-5589-4c7c-8358-2ca1572b87eb --output BaseLinear.001 Base-Linear.aff4
../src/aff4imager --verbose --export aff4://8fcced2b-989f-4f51-bfa2-38d4a4d818fe --output BaseAllocated.001 Base-Allocated.aff4
../src/aff4imager --verbose --export aff4://3a873665-7bf6-47b5-a12a-d6632a58ddf9 --output BaseReadError.001 Base-Linear-ReadError.aff4

../src/aff4imager --verbose --export aff4://a3956d8e-0bc2-425f-9feb-73f4aeab7437 --output LegacyBaseLinear.001 Legacy-Base-Linear.af4
../src/aff4imager --verbose --export aff4://7823bbeb-666c-4ffa-845c-34fe445a0db1 --output LegacyBaseAllocated.001 Legacy-Base-Allocated.af4
../src/aff4imager --verbose --export aff4://31c2aa08-30ec-4610-a66f-108a69a8c623 --output LegacyBaseReadError.001 Legacy-Base-Linear-ReadError.af4

# The digest/hash values should be the same for each type of acquisition strategy.

echo "SHA1 sums"
echo "---------"
sha1sum BaseLinear.001
sha1sum LegacyBaseLinear.001
echo
sha1sum BaseAllocated.001
sha1sum LegacyBaseAllocated.001
echo
sha1sum BaseReadError.001
sha1sum LegacyBaseReadError.001
echo

echo "MD5 sums"
echo "---------"
md5sum BaseLinear.001
md5sum LegacyBaseLinear.001
echo
md5sum BaseAllocated.001
md5sum LegacyBaseAllocated.001
echo
md5sum BaseReadError.001
md5sum LegacyBaseReadError.001
echo

# clean up
rm *.001
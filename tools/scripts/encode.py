#!/usr/bin/python3

"""Encode binary files into .cc files.

This script takes a sequence of binary files and encodes them into a
.cc and .h files.

This is useful for including the files inside a program in a portable
way, without relying on PE resource etc.
"""

from os import  path

import argparse
import sys

def convert_file(path, variable_name):
    data = open(path, 'rb').read()
    result = 'const unsigned char %s[] = {' % variable_name
    for i, char in enumerate(data):
        if i % 8 == 0:
            result += '\n   '
        result += ' 0x%02x,' % char
    result += '\n};\n'

    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser()

    parser.add_argument('--output', type=str,
                        help='output file name.')

    parser.add_argument('files', metavar='N', type=str, nargs='+',
                        help='Files to encode.')

    args = parser.parse_args()

    with open(args.output + ".h", "w") as fd:
        for filename in args.files:
            variable_name, _ = path.splitext(path.basename(filename))
            fd.write("extern unsigned char[] " + variable_name + ";\n")

    with open(args.output + ".cc", "w") as fd:
        for filename in args.files:
            variable_name, _ = path.splitext(path.basename(filename))
            fd.write(convert_file(filename, variable_name))

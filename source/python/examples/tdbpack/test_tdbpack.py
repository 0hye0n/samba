#! /usr/bin/env python2.2

__doc__ = """test case for samba.tdbkpack functions

tdbpack provides a means of pickling values into binary formats
compatible with that used by the samba tdbpack()/tdbunpack()
functions.

Numbers are always stored in little-endian format; strings are stored
in either DOS or Unix codepage as appropriate.

The format for any particular element is encoded as a short ASCII
string, with one character per field."""

# Copyright (C) 2002 Hewlett-Packard.

__author__ = 'Martin Pool <mbp@sourcefrog.net>'

import unittest
import oldtdbutil
import samba.tdbpack

both_unpackers = (samba.tdbpack.unpack, oldtdbutil.unpack)
both_packers = (samba.tdbpack.pack, oldtdbutil.pack)
    
class PackTests(unittest.TestCase):
    symm_cases = [('B', ['hello' * 51], '\xff\0\0\0' + 'hello' * 51),
             ('w', [42], '\x2a\0'),
             ('www', [42, 2, 69], '\x2a\0\x02\0\x45\0'),
             ('wd', [42, 256], '\x2a\0\0\x01\0\0'),
             ('w', [0], '\0\0'),
             ('w', [255], '\xff\0'),
             ('w', [256], '\0\x01'),
             ('w', [0xdead], '\xad\xde'),
             ('w', [0xffff], '\xff\xff'),
             ('p', [0], '\0\0\0\0'),
             ('p', [1], '\x01\0\0\0'),
             ('d', [0x01020304], '\x04\x03\x02\x01'),
             ('d', [0x7fffffff], '\xff\xff\xff\x7f'),
             ('d', [0x80000000], '\x00\x00\x00\x80'),
             ('d', [-1], '\xff\xff\xff\xff'),
             ('d', [-255], '\x01\xff\xff\xff'),
             ('d', [-256], '\x00\xff\xff\xff'),
             ('ddd', [1, 10, 50], '\x01\0\0\0\x0a\0\0\0\x32\0\0\0'),
             ('ff', ['hello', 'world'], 'hello\0world\0'),
             ('fP', ['hello', 'world'], 'hello\0world\0'),
             ('PP', ['hello', 'world'], 'hello\0world\0'),
             ('B', [''], '\0\0\0\0'),
             ('B', ['hello'], '\x05\0\0\0hello'),
             ('BB', ['hello\0world', 'now'],
              '\x0b\0\0\0hello\0world\x03\0\0\0now'),
             ('pd', [1, 10], '\x01\0\0\0\x0a\0\0\0'),
             ('BBB', ['hello', '', 'world'],
              '\x05\0\0\0hello\0\0\0\0\x05\0\0\0world'),

             # strings are sequences in Python, there's no getting away
             # from it
             ('ffff', 'evil', 'e\0v\0i\0l\0'),
             ('BBBB', 'evil',                   
              '\x01\0\0\0e'
              '\x01\0\0\0v'
              '\x01\0\0\0i'
              '\x01\0\0\0l'),

             ('', [], ''),

             # exercise some long strings
             ('PP', ['hello' * 255, 'world' * 255],
              'hello' * 255 + '\0' + 'world' * 255 + '\0'),
             ('PP', ['hello' * 40000, 'world' * 50000],
              'hello' * 40000 + '\0' + 'world' * 50000 + '\0'),
             ('B', ['hello' * 51], '\xff\0\0\0' + 'hello' * 51),
             ('BB', ['hello' * 40000, 'world' * 50000],
              '\x40\x0d\x03\0' + 'hello' * 40000 + '\x90\xd0\x03\x00' + 'world' * 50000),
             ]

    def test_symmetric(self):
        """Cookbook of symmetric pack/unpack tests
        """
        for packer in both_packers:
            for unpacker in both_unpackers:
                for format, values, expected in self.symm_cases:
                    self.assertEquals(packer(format, values), expected)
                    out, rest = unpacker(format, expected)
                    self.assertEquals(rest, '')
                    self.assertEquals(list(values), list(out))

    def test_large(self):
        """Test large pack/unpack strings"""
        large_cases = [('w' * 1000, xrange(1000)), ]
        for packer in both_packers:
            for unpacker in both_unpackers:
                for format, values in large_cases:
                    packed = packer(format, values)
                    out, rest = unpacker(format, packed)
                    self.assertEquals(rest, '')
                    self.assertEquals(list(values), list(out))

                    
    def test_pack(self):
        """Cookbook of expected pack values

        These can't be used for the symmetric test because the unpacked value is
        not "canonical".
        """
        cases = [('w', (42,), '\x2a\0'),
                 ('p', [None], '\0\0\0\0'),
                 ('p', ['true'], '\x01\0\0\0'),
                 ]

        for packer in both_packers:
            for format, values, expected in cases:
                self.assertEquals(packer(format, values), expected)

    def test_unpack_extra(self):
        # Test leftover data
        for unpacker in both_unpackers:
            for format, values, packed in self.symm_cases:
                out, rest = unpacker(format, packed + 'hello sailor!')
                self.assertEquals(rest, 'hello sailor!')
                self.assertEquals(list(values), list(out))


    def test_pack_extra(self):
        """Leftover values when packing"""
        cases = [
            ('d', [10, 20], [10]),
            ]
        for unpacker in both_unpackers:
            for packer in both_packers:
                for format, values, chopped in cases:
                    bin = packer(format, values)
                    out, rest = unpacker(format, bin)
                    self.assertEquals(list(out), list(chopped))
                    self.assertEquals(rest, '')


    def test_unpack(self):
        """Cookbook of tricky unpack tests"""
        cases = [
                 # Apparently I couldn't think of any tests that weren't
                 # symmetric :-/
                 ]
        for unpacker in both_unpackers:
            for format, values, expected in cases:
                out, rest = unpacker(format, expected)
                self.assertEquals(rest, '')
                self.assertEquals(list(values), list(out))


    def test_pack_failures(self):
        """Expected errors for incorrect packing"""
        cases = [('w', []),
                 ('w', ()),
                 ('w', {}),
                 ('ww', [2]),
                 ('w', 2),
                 ('w', None),
                 ('wwwwwwwwwwww', []),
#                 ('w', [0x60A15EC5L]),
                 ('w', [None]),
                 ('d', []),
                 ('d', [0L]),
                 ('p', []),
                 ('f', [2]),
                 ('P', [None]),
                 ('P', ()),
                 ('f', [hex]),
                 ('fw', ['hello']),
                 ('f', [u'hello']),
                 ('B', [2]),
                 (None, [2, 3, 4]),
                 (ord('f'), [20]),
                 (['w', 'w'], [2, 2]),
                 ('Q', [2]),
                 ('fQ', ['2', 3]),
                 ('fQ', ['2']),
                 (2, [2]),
                 ({}, {})]
        for packer in both_packers:
            for format, values in cases:
                try:
                    packer(format, values)
                except StandardError:
                    pass
                else:
                    raise AssertionError("didn't get exception: format %s, values %s, packer %s"
                                         % (`format`, `values`, `packer`))


    def test_unpack_failures(self):
        """Expected errors for incorrect unpacking"""
        cases = [('$', '', ValueError),
                 ('Q', '', ValueError),
                 ('Q$', '', ValueError),
                 ('f', '', IndexError),
                 ('d', '', IndexError),
                 ('d', '2', IndexError),
                 ('d', '22', IndexError),
                 ('d', '222', IndexError),
                 ('w', '', IndexError),
                 ('w', '2', IndexError),
                 ('f', 'hello', IndexError),
                 ('f', '', IndexError),
                 ('p', '\x01\0', IndexError),
                 ('B', '\xff\0\0\0hello', IndexError),
                 ('B', '\xff\0', IndexError),
                 ('B', '\x01\0\0\0', IndexError),
                 ('B', '\x05\0\0\0hell', IndexError),
                 ('B', '\xff\xff\xff\xff', ValueError),
                 ('B', 'foobar', IndexError),
                 ('BB', '\x01\0\0\0a\x01', IndexError),
                 ]

        for unpacker in both_unpackers:
            for format, values, throwable_class in cases:
                def do_unpack():
                    unpacker(format, values)
            self.assertRaises(throwable_class, do_unpack)

        

if __name__ == '__main__':
    unittest.main()
    

#!/usr/bin/env python

# Unix SMB/CIFS implementation.
# Copyright (C) Jelmer Vernooij <jelmer@samba.org> 2011
#
# Loosely based on bzrlib's test_source.py
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

"""Source level Python tests."""

import errno
import os
import re
import warnings

import samba
samba.ensure_external_module("pep8", "pep8")
import pep8

from samba.tests import (
    TestCase,
    )



def get_python_source_files():
    """Iterate over all Python source files."""
    library_dir = os.path.join(os.path.dirname(__file__), "..", "..", "samba")

    for root, dirs, files in os.walk(library_dir):
        for f in files:
            if f.endswith(".py"):
                yield os.path.abspath(os.path.join(root, f))

    bindir = os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "bin")
    for f in os.listdir(bindir):
        p = os.path.abspath(os.path.join(bindir, f))
        if not os.path.islink(p):
            continue
        target = os.readlink(p)
        if os.path.dirname(target).endswith("scripting/bin"):
            yield p


def get_source_file_contents():
    """Iterate over the contents of all python files."""
    for fname in get_python_source_files():
        try:
            f = open(fname, 'rb')
        except IOError, e:
            if e.errno == errno.ENOENT:
                warnings.warn("source file %s broken link?" % fname)
                continue
            else:
                raise
        try:
            text = f.read()
        finally:
            f.close()
        yield fname, text


class TestSource(TestCase):

    def test_copyright(self):
        """Test that all Python files have a valid copyright statement."""
        incorrect = []

        copyright_re = re.compile('#\\s*copyright.*(?=\n)', re.I)

        for fname, text in get_source_file_contents():
            if fname.endswith("ms_schema.py"):
                # FIXME: Not sure who holds copyright on ms_schema.py
                continue
            match = copyright_re.search(text)
            if not match:
                incorrect.append((fname, 'no copyright line found\n'))

        if incorrect:
            help_text = ["Some files have missing or incorrect copyright"
                         " statements.",
                         "",
                        ]
            for fname, comment in incorrect:
                help_text.append(fname)
                help_text.append((' ' * 4) + comment)

            self.fail('\n'.join(help_text))

    def test_gpl(self):
        """Test that all .py files have a GPL disclaimer."""
        incorrect = []

        gpl_txt = """
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""
        gpl_re = re.compile(re.escape(gpl_txt), re.MULTILINE)

        for fname, text in get_source_file_contents():
            if not gpl_re.search(text):
                incorrect.append(fname)

        if incorrect:
            help_text = ['Some files have missing or incomplete GPL statement',
                         gpl_txt]
            for fname in incorrect:
                help_text.append((' ' * 4) + fname)

            self.fail('\n'.join(help_text))

    def _push_file(self, dict_, fname, line_no):
        if fname not in dict_:
            dict_[fname] = [line_no]
        else:
            dict_[fname].append(line_no)

    def _format_message(self, dict_, message):
        files = ["%s: %s" % (f, ', '.join([str(i + 1) for i in lines]))
                for f, lines in dict_.items()]
        files.sort()
        return message + '\n\n    %s' % ('\n    '.join(files))

    def _iter_source_files_lines(self):
        for fname, text in get_source_file_contents():
            lines = text.splitlines(True)
            last_line_no = len(lines) - 1
            for line_no, line in enumerate(lines):
                yield fname, line_no, line

    def test_no_tabs(self):
        """Check that there are no tabs in Python files."""
        tabs = {}
        for fname, line_no, line in self._iter_source_files_lines():
            if '\t' in line:
                self._push_file(tabs, fname, line_no)
        if tabs:
            self.fail(self._format_message(tabs,
                'Tab characters were found in the following source files.'
                '\nThey should either be replaced by "\\t" or by spaces:'))

    def test_unix_newlines(self):
        """Check for unix new lines."""
        illegal_newlines = {}
        for fname, line_no, line in self._iter_source_files_lines():
            if not line.endswith('\n') or line.endswith('\r\n'):
                self._push_file(illegal_newlines, fname, line_no)
        if illegal_newlines:
            self.fail(self._format_message(illegal_newlines,
                'Non-unix newlines were found in the following source files:'))

    pep8_ignore = [
        'E401',      # multiple imports on one line
        'E501',      # line too long
        'E251',      # no spaces around keyword / parameter equals
        'E201',      # whitespace after '['
        'E202',      # whitespace before ')'
        'E302',      # expected 2 blank lines, found 1
        'E231',      # missing whitespace after ','
        'E225',      # missing whitespace around operator
        'E111',      # indentation is not a multiple of four
        'E261',      # at least two spaces before inline comment
        'E702',      # multiple statements on one line (semicolon)
        'E221',      # multiple spaces before operator
        'E303',      # too many blank lines (2)
        'E203',      # whitespace before ':'
        'E222',      # multiple spaces after operator
        'E301',      # expected 1 blank line, found 0
        'E211',      # whitespace before '('
        'E701',      # multiple statements on one line (colon)
        ]

    def test_pep8(self):
        pep8.process_options()
        pep8.options.repeat = True
        pep8_errors = []
        pep8_warnings = []
        for fname, text in get_source_file_contents():
            def report_error(line_number, offset, text, check):
                code = text[:4]
                if code in self.pep8_ignore:
                    code = 'W' + code[1:]
                text = code + text[4:]
                print "%s:%s: %s" % (fname, line_number, text)
                summary = (fname, line_number, offset, text, check)
                if code[0] == 'W':
                    pep8_warnings.append(summary)
                else:
                    pep8_errors.append(summary)
            lines = text.splitlines(True)
            checker = pep8.Checker(fname, lines)
            checker.report_error = report_error
            checker.check_all()
        if len(pep8_errors) > 0:
            self.fail('there were %d pep8 errors' % len(pep8_errors))

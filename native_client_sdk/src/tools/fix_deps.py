#!/usr/bin/env python
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Fixup GCC-generated dependency files.

Modify GCC generated dependency files in-place so they are more suitable
for including in a GNU Makefile.  Without the fixups, deleting or renaming
headers can cause the build to be broken. See:
http://mad-scientist.net/make/autodep.html for more details of the problem.
"""

import os
import optparse
import sys

class Error(Exception):
  pass


def ParseLine(line, new_target):
  """Parse one line of a GCC-generated deps file.

  Each line contains an optional target and then a list
  of space seperated dependencies.  Spaces within filenames
  are escaped with a backslash.
  """
  filenames = []

  if new_target and ':' in line:
    line = line.split(':', 1)[1]

  line = line.strip()
  line = line.rstrip('\\')

  while True:
    # Find the next non-escaped space
    line = line.strip()
    pos = line.find(' ')
    while pos > 0 and line[pos-1] == '\\':
      pos = line.find(' ', pos+1)

    if pos == -1:
      filenames.append(line)
      break
    filenames.append(line[:pos])
    line = line[pos+1:]

  return filenames


def FixupDepFile(filename):
  if not os.path.exists(filename):
    raise Error('File not found: %s' % filename)

  outlines = []
  deps = []
  new_target = True
  with open(filename) as infile:
    for line in infile:
      outlines.append(line)
      deps += ParseLine(line, new_target)
      new_target = line.endswith('\\')

  # For every depenency found output a dummy target with no rules
  for dep in deps:
    outlines.append('%s:\n' % dep)

  with open(filename, 'w') as outfile:
    for line in outlines:
      outfile.write(line)


def main(argv):
  usage = "usage: %prog [options] <dep_file ...>"
  parser = optparse.OptionParser(usage=usage, description=__doc__)
  args = parser.parse_args(argv)[1]
  if not args:
    raise parser.error('expected one or more files as arguments')
  for arg in args:
    FixupDepFile(arg)


if __name__ == '__main__':
  try:
    sys.exit(main(sys.argv[1:]))
  except Error as e:
    sys.stderr.write('%s: %s\n' % (os.path.basename(__file__), e))
    sys.exit(1)

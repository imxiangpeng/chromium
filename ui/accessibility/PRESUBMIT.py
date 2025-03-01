# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Presubmit script for ui/accessibility."""

import os, re

AX_IDL = 'ui/accessibility/ax_enums.idl'
AUTOMATION_IDL = 'chrome/common/extensions/api/automation.idl'

AX_JS_FILE = 'content/browser/resources/accessibility/accessibility.js'
AX_MODE_HEADER = 'ui/accessibility/ax_modes.h'

def InitialLowerCamelCase(unix_name):
  words = unix_name.split('_')
  return words[0] + ''.join(word.capitalize() for word in words[1:])

# Given a full path to an IDL file containing enum definitions,
# parse the file for enums and return a dict mapping the enum name
# to a list of values for that enum.
def GetEnumsFromFile(fullpath):
  enum_name = None
  enums = {}
  for line in open(fullpath).readlines():
    # Strip out comments
    line = re.sub('//.*', '', line)

    # Look for lines of the form "enum ENUM_NAME {" and get the enum_name
    m = re.search('enum ([\w]+) {', line)
    if m:
      enum_name = m.group(1)
      continue

    # Look for a "}" character signifying the end of an enum
    if line.find('}') >= 0:
      enum_name = None
      continue

    if not enum_name:
      continue

    # If we're inside an enum definition, add the first string consisting of
    # alphanumerics plus underscore ("\w") to the list of values for that enum.
    m = re.search('([\w]+)', line)
    if m:
      enums.setdefault(enum_name, [])
      enums[enum_name].append(m.group(1))

  return enums

def CheckMatchingEnum(ax_enums,
                      ax_enum_name,
                      automation_enums,
                      automation_enum_name,
                      errs,
                      output_api):
  if ax_enum_name not in ax_enums:
    errs.append(output_api.PresubmitError(
        'Expected %s to have an enum named %s' % (AX_IDL, ax_enum_name)))
    return
  if automation_enum_name not in automation_enums:
    errs.append(output_api.PresubmitError(
        'Expected %s to have an enum named %s' % (
            AUTOMATION_IDL, automation_enum_name)))
    return
  src = ax_enums[ax_enum_name]
  dst = automation_enums[automation_enum_name]
  for value in src:
    lower_value = InitialLowerCamelCase(value)
    if lower_value in dst:
      dst.remove(lower_value)  # Any remaining at end are extra and a mismatch.
    else:
      errs.append(output_api.PresubmitError(
          'Found %s.%s in %s, but did not find %s.%s in %s' % (
              ax_enum_name, value, AX_IDL,
              automation_enum_name, InitialLowerCamelCase(value),
              AUTOMATION_IDL)))
  #  Should be no remaining items
  for value in dst:
      errs.append(output_api.PresubmitError(
          'Found %s.%s in %s, but did not find %s.%s in %s' % (
              automation_enum_name, value, AUTOMATION_IDL,
              ax_enum_name, InitialLowerCamelCase(value),
              AX_IDL)))

def CheckEnumsMatch(input_api, output_api):
  repo_root = input_api.change.RepositoryRoot()
  ax_enums = GetEnumsFromFile(os.path.join(repo_root, AX_IDL))
  automation_enums = GetEnumsFromFile(os.path.join(repo_root, AUTOMATION_IDL))
  errs = []
  CheckMatchingEnum(ax_enums, 'AXRole', automation_enums, 'RoleType', errs,
                    output_api)
  CheckMatchingEnum(ax_enums, 'AXState', automation_enums, 'StateType', errs,
                    output_api)
  CheckMatchingEnum(ax_enums, 'AXEvent', automation_enums, 'EventType', errs,
                    output_api)
  CheckMatchingEnum(ax_enums, 'AXNameFrom', automation_enums, 'NameFromType',
                    errs, output_api)
  CheckMatchingEnum(ax_enums, 'AXRestriction', automation_enums,
                   'Restriction', errs, output_api)
  return errs

# Given a full path to c++ header, return an array of the first static
# constexpr defined. (Note there can be more than one defined in a C++
# header)
def GetConstexprFromFile(fullpath):
  values = []
  for line in open(fullpath).readlines():
    # Strip out comments
    line = re.sub('//.*', '', line)

    # Look for lines of the form "static constexpr <type> NAME "
    m = re.search('static constexpr [\w]+ ([\w]+)', line)
    if m:
      values.append(m.group(1))

  return values

# Given a full path to js file, return the AXMode consts
# defined
def GetAccessibilityModesFromFile(fullpath):
  values = []
  inside = False
  for line in open(fullpath).readlines():
    # Strip out comments
    line = re.sub('//.*', '', line)

    # Look for the block of code that defines AXMode
    m = re.search('const AXMode = {', line)
    if m:
      inside = True
      continue

    # Look for a "}" character signifying the end of an enum
    if line.find('};') >= 0:
      return values

    if not inside:
      continue

    m = re.search('([\w]+):', line)
    if m:
      values.append(m.group(1))
      continue

    # getters
    m = re.search('get ([\w]+)\(\)', line)
    if m:
      values.append(m.group(1))
  return values

# Make sure that the modes defined in the C++ header match those defined in
# the js file. Note that this doesn't guarantee that the values are the same,
# but does make sure if we add or remove we can signal to the developer that
# they should be aware that this dependency exists.
def CheckModesMatch(input_api, output_api):
  errs = []
  repo_root = input_api.change.RepositoryRoot()

  ax_modes_in_header = GetConstexprFromFile(
    os.path.join(repo_root,AX_MODE_HEADER))
  ax_modes_in_js = GetAccessibilityModesFromFile(
    os.path.join(repo_root, AX_JS_FILE))

  for value in ax_modes_in_header:
    if value not in ax_modes_in_js:
      errs.append(output_api.PresubmitError(
          'Found %s in %s, but did not find %s in %s' % (
              value, AX_MODE_HEADER, value, AX_JS_FILE)))
  return errs

def CheckChangeOnUpload(input_api, output_api):
  errs = []
  if AX_IDL in input_api.LocalPaths():
    errs.extend(CheckEnumsMatch(input_api, output_api))

  if AX_MODE_HEADER in input_api.LocalPaths():
    errs.extend(CheckModesMatch(input_api, output_api))

  return errs

def CheckChangeOnCommit(input_api, output_api):
  errs = []
  if AX_IDL in input_api.LocalPaths():
    errs.extend(CheckEnumsMatch(input_api, output_api))

  if AX_MODE_HEADER in input_api.LocalPaths():
    errs.extend(CheckModesMatch(input_api, output_api))

  return errs

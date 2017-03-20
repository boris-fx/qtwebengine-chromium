#!/usr/bin/env python

# Copyright 2015 The Crashpad Authors. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import platform
import random
import re
import subprocess
import sys
import tempfile
import time


g_temp_dirs = []
g_had_failures = False


def MakeTempDir():
  global g_temp_dirs
  new_dir = tempfile.mkdtemp()
  g_temp_dirs.append(new_dir)
  return new_dir


def CleanUpTempDirs():
  global g_temp_dirs
  for d in g_temp_dirs:
    subprocess.call(['rmdir', '/s', '/q', d], shell=True)


def FindInstalledWindowsApplication(app_path):
  search_paths = [os.getenv('PROGRAMFILES(X86)'),
                  os.getenv('PROGRAMFILES'),
                  os.getenv('PROGRAMW6432'),
                  os.getenv('LOCALAPPDATA')]
  search_paths += os.getenv('PATH', '').split(os.pathsep)

  for search_path in search_paths:
    if not search_path:
      continue
    path = os.path.join(search_path, app_path)
    if os.path.isfile(path):
      return path

  return None


def GetCdbPath():
  """Search in some reasonable places to find cdb.exe. Searches x64 before x86
  and newer versions before older versions.
  """
  possible_paths = (
      os.path.join('Windows Kits', '10', 'Debuggers', 'x64'),
      os.path.join('Windows Kits', '10', 'Debuggers', 'x86'),
      os.path.join('Windows Kits', '8.1', 'Debuggers', 'x64'),
      os.path.join('Windows Kits', '8.1', 'Debuggers', 'x86'),
      os.path.join('Windows Kits', '8.0', 'Debuggers', 'x64'),
      os.path.join('Windows Kits', '8.0', 'Debuggers', 'x86'),
      'Debugging Tools For Windows (x64)',
      'Debugging Tools For Windows (x86)',
      'Debugging Tools For Windows',)
  for possible_path in possible_paths:
    app_path = os.path.join(possible_path, 'cdb.exe')
    app_path = FindInstalledWindowsApplication(app_path)
    if app_path:
      return app_path
  return None


def GetDumpFromProgram(out_dir, pipe_name, executable_name, *args):
  """Initialize a crash database, and run |executable_name| connecting to a
  crash handler. If pipe_name is set, crashpad_handler will be started first. If
  pipe_name is empty, the executable is responsible for starting
  crashpad_handler. *args will be passed after other arguments to
  executable_name. Returns the minidump generated by crashpad_handler for
  further testing.
  """
  test_database = MakeTempDir()
  handler = None

  try:
    if subprocess.call(
        [os.path.join(out_dir, 'crashpad_database_util.exe'), '--create',
         '--database=' + test_database]) != 0:
      print 'could not initialize report database'
      return None

    if pipe_name is not None:
      handler = subprocess.Popen([
          os.path.join(out_dir, 'crashpad_handler.com'),
          '--pipe-name=' + pipe_name,
          '--database=' + test_database
      ])

      # Wait until the server is ready.
      printed = False
      while not os.path.exists(pipe_name):
        if not printed:
          print 'Waiting for crashpad_handler to be ready...'
          printed = True
        time.sleep(0.1)

      subprocess.call([os.path.join(out_dir, executable_name), pipe_name] +
                      list(args))
    else:
      subprocess.call([os.path.join(out_dir, executable_name),
                       os.path.join(out_dir, 'crashpad_handler.com'),
                       test_database] + list(args))

    out = subprocess.check_output([
        os.path.join(out_dir, 'crashpad_database_util.exe'),
        '--database=' + test_database,
        '--show-completed-reports',
        '--show-all-report-info',
    ])
    for line in out.splitlines():
      if line.strip().startswith('Path:'):
        return line.partition(':')[2].strip()
  finally:
    if handler:
      handler.kill()


def GetDumpFromCrashyProgram(out_dir, pipe_name):
  return GetDumpFromProgram(out_dir, pipe_name, 'crashy_program.exe')


def GetDumpFromOtherProgram(out_dir, pipe_name, *args):
  return GetDumpFromProgram(out_dir, pipe_name, 'crash_other_program.exe',
                            *args)


def GetDumpFromSelfDestroyingProgram(out_dir, pipe_name):
  return GetDumpFromProgram(out_dir, pipe_name, 'self_destroying_program.exe')


def GetDumpFromZ7Program(out_dir, pipe_name):
  return GetDumpFromProgram(out_dir, pipe_name, 'crashy_z7_loader.exe')


class CdbRun(object):
  """Run cdb.exe passing it a cdb command and capturing the output.
  `Check()` searches for regex patterns in sequence allowing verification of
  expected output.
  """

  def __init__(self, cdb_path, dump_path, command):
    # Run a command line that loads the dump, runs the specified cdb command,
    # and then quits, and capturing stdout.
    self.out = subprocess.check_output([
        cdb_path,
        '-z', dump_path,
        '-c', command + ';q'
    ])

  def Check(self, pattern, message, re_flags=0):
    match_obj = re.search(pattern, self.out, re_flags)
    if match_obj:
      # Matched. Consume up to end of match.
      self.out = self.out[match_obj.end(0):]
      print 'ok - %s' % message
      sys.stdout.flush()
    else:
      print >>sys.stderr, '-' * 80
      print >>sys.stderr, 'FAILED - %s' % message
      print >>sys.stderr, '-' * 80
      print >>sys.stderr, 'did not match:\n  %s' % pattern
      print >>sys.stderr, '-' * 80
      print >>sys.stderr, 'remaining output was:\n  %s' % self.out
      print >>sys.stderr, '-' * 80
      sys.stderr.flush()
      global g_had_failures
      g_had_failures = True

  def Find(self, pattern, re_flags=0):
    match_obj = re.search(pattern, self.out, re_flags)
    if match_obj:
      # Matched. Consume up to end of match.
      self.out = self.out[match_obj.end(0):]
      return match_obj
    return None


def RunTests(cdb_path,
             dump_path,
             start_handler_dump_path,
             destroyed_dump_path,
             z7_dump_path,
             other_program_path,
             other_program_no_exception_path,
             pipe_name):
  """Runs various tests in sequence. Runs a new cdb instance on the dump for
  each block of tests to reduce the chances that output from one command is
  confused for output from another.
  """
  out = CdbRun(cdb_path, dump_path, '.ecxr')
  out.Check('This dump file has an exception of interest stored in it',
            'captured exception')

  # When SomeCrashyFunction is inlined, cdb doesn't demangle its namespace as
  # "`anonymous namespace'" and instead gives the decorated form.
  out.Check('crashy_program!crashpad::(`anonymous namespace\'|\?A0x[0-9a-f]+)::'
                'SomeCrashyFunction',
            'exception at correct location')

  out = CdbRun(cdb_path, start_handler_dump_path, '.ecxr')
  out.Check('This dump file has an exception of interest stored in it',
            'captured exception (using StartHandler())')
  out.Check('crashy_program!crashpad::(`anonymous namespace\'|\?A0x[0-9a-f]+)::'
                'SomeCrashyFunction',
            'exception at correct location (using StartHandler())')

  out = CdbRun(cdb_path, dump_path, '!peb')
  out.Check(r'PEB at', 'found the PEB')
  out.Check(r'Ldr\.InMemoryOrderModuleList:.*\d+ \. \d+', 'PEB_LDR_DATA saved')
  out.Check(r'Base TimeStamp                     Module', 'module list present')
  pipe_name_escaped = pipe_name.replace('\\', '\\\\')
  out.Check(r'CommandLine: *\'.*crashy_program.exe *' + pipe_name_escaped,
            'some PEB data is correct')
  out.Check(r'SystemRoot=C:\\Windows', 'some of environment captured',
            re.IGNORECASE)

  out = CdbRun(cdb_path, dump_path, '!teb')
  out.Check(r'TEB at', 'found the TEB')
  out.Check(r'ExceptionList:\s+[0-9a-fA-F]+', 'some valid teb data')
  out.Check(r'LastErrorValue:\s+2', 'correct LastErrorValue')

  out = CdbRun(cdb_path, dump_path, '!gle')
  out.Check('LastErrorValue: \(Win32\) 0x2 \(2\) - The system cannot find the '
            'file specified.', '!gle gets last error')
  out.Check('LastStatusValue: \(NTSTATUS\) 0xc000000f - {File Not Found}  The '
            'file %hs does not exist.', '!gle gets last ntstatus')

  if False:
    # TODO(scottmg): Re-enable when we grab ntdll!RtlCriticalSectionList.
    out = CdbRun(cdb_path, dump_path, '!locks')
    out.Check(r'CritSec crashy_program!crashpad::`anonymous namespace\'::'
              r'g_test_critical_section', 'lock was captured')
    if platform.win32_ver()[0] != '7':
      # We can't allocate CRITICAL_SECTIONs with .DebugInfo on Win 7.
      out.Check(r'\*\*\* Locked', 'lock debug info was captured, and is locked')

  out = CdbRun(cdb_path, dump_path, '!handle')
  out.Check(r'\d+ Handles', 'captured handles')
  out.Check(r'Event\s+\d+', 'capture some event handles')
  out.Check(r'File\s+\d+', 'capture some file handles')

  out = CdbRun(cdb_path, dump_path, 'lm')
  out.Check(r'Unloaded modules:', 'captured some unloaded modules')
  out.Check(r'lz32\.dll', 'found expected unloaded module lz32')
  out.Check(r'wmerror\.dll', 'found expected unloaded module wmerror')

  out = CdbRun(cdb_path, destroyed_dump_path, '.ecxr;!peb;k 2')
  out.Check(r'Ldr\.InMemoryOrderModuleList:.*\d+ \. \d+', 'PEB_LDR_DATA saved')
  out.Check(r'ntdll\.dll', 'ntdll present', re.IGNORECASE)

  # Check that there is no stack trace in the self-destroyed process. Confirm
  # that the top is where we expect it (that's based only on IP), but subsequent
  # stack entries will not be available. This confirms that we have a mostly
  # valid dump, but that the stack was omitted.
  out.Check(r'self_destroying_program!crashpad::`anonymous namespace\'::'
                r'FreeOwnStackAndBreak.*\nquit:',
            'at correct location, no additional stack entries')

  # Dump memory pointed to be EDI on the background suspended thread. We don't
  # know the index of the thread because the system may have started other
  # threads, so first do a run to extract the thread index that's suspended, and
  # then another run to dump the data pointed to by EDI for that thread.
  out = CdbRun(cdb_path, dump_path, '.ecxr;~')
  match_obj = out.Find(r'(\d+)\s+Id: [0-9a-f.]+ Suspend: 1 Teb:')
  if match_obj:
    thread = match_obj.group(1)
    out = CdbRun(cdb_path, dump_path, '.ecxr;~' + thread + 's;db /c14 edi')
  out.Check(r'63 62 61 60 5f 5e 5d 5c-5b 5a 59 58 57 56 55 54 53 52 51 50',
            'data pointed to by registers captured')

  # Move up one stack frame after jumping to the exception, and examine memory.
  out = CdbRun(cdb_path, dump_path,
               '.ecxr; .f+; dd /c100 poi(offset_pointer)-20')
  out.Check(r'80000078 00000079 8000007a 0000007b 8000007c 0000007d 8000007e '
            r'0000007f 80000080 00000081 80000082 00000083 80000084 00000085 '
            r'80000086 00000087 80000088 00000089 8000008a 0000008b 8000008c '
            r'0000008d 8000008e 0000008f 80000090 00000091 80000092 00000093 '
            r'80000094 00000095 80000096 00000097',
            'data pointed to by stack captured')

  # Attempt to retrieve the value of g_extra_memory_pointer (by name), and then
  # examine the memory at which it points. Both should have been saved.
  out = CdbRun(cdb_path, dump_path,
               'dd poi(crashy_program!crashpad::g_extra_memory_pointer)+0x1f30 '
               'L8')
  out.Check(r'0000655e 0000656b 00006578 00006585',
            'extra memory range captured')
  out.Check(r'\?\?\?\?\?\?\?\? \?\?\?\?\?\?\?\? '
            r'\?\?\?\?\?\?\?\? \?\?\?\?\?\?\?\?',
            '  and not memory after range')

  if False:
    # TODO(scottmg): This is flakily capturing too much memory in Debug builds,
    # possibly because a stale pointer is being captured via the stack.
    # See: https://bugs.chromium.org/p/crashpad/issues/detail?id=101.
    out = CdbRun(cdb_path, dump_path,
                'dd poi(crashy_program!crashpad::g_extra_memory_not_saved)'
                '+0x1f30 L4')
    # We save only the pointer, not the pointed-to data. If the pointer itself
    # wasn't saved, then we won't get any memory printed, so here we're
    # confirming the pointer was saved but the memory wasn't.
    out.Check(r'\?\?\?\?\?\?\?\? \?\?\?\?\?\?\?\? '
              r'\?\?\?\?\?\?\?\? \?\?\?\?\?\?\?\?',
              'extra memory removal')

  out = CdbRun(cdb_path, dump_path, '.dumpdebug')
  out.Check(r'type \?\?\? \(333333\), size 00001000',
            'first user stream')
  out.Check(r'type \?\?\? \(222222\), size 00000080',
            'second user stream')

  if z7_dump_path:
    out = CdbRun(cdb_path, z7_dump_path, '.ecxr;lm')
    out.Check('This dump file has an exception of interest stored in it',
              'captured exception in z7 module')
    # Older versions of cdb display relative to exports for /Z7 modules, newer
    # ones just display the offset.
    out.Check(r'z7_test(!CrashMe\+0xe|\+0x100e):',
              'exception in z7 at correct location')
    out.Check(r'z7_test  C \(codeview symbols\)     z7_test.dll',
              'expected non-pdb symbol format')

  out = CdbRun(cdb_path, other_program_path, '.ecxr;k;~')
  out.Check('Unknown exception - code deadbea7',
            'other program dump exception code')
  out.Check('!Sleep', 'other program reasonable location')
  out.Check('hanging_program!Thread1', 'other program dump right thread')
  count = 0
  while True:
    match_obj = out.Find(r'Id.*Suspend: (\d+) ')
    if match_obj:
      if match_obj.group(1) != '0':
        out.Check(r'FAILED', 'all suspend counts should be 0')
      else:
        count += 1
    else:
      break
  assert count > 2

  out = CdbRun(cdb_path, other_program_no_exception_path, '.ecxr;k')
  out.Check('Unknown exception - code 0cca11ed',
            'other program with no exception given')
  out.Check('!RaiseException', 'other program in RaiseException()')


def main(args):
  try:
    if len(args) != 1:
      print >>sys.stderr, 'must supply binary dir'
      return 1

    cdb_path = GetCdbPath()
    if not cdb_path:
      print >>sys.stderr, 'could not find cdb'
      return 1

    # Make sure we can download Windows symbols.
    if not os.environ.get('_NT_SYMBOL_PATH'):
      symbol_dir = MakeTempDir()
      protocol = 'https' if platform.win32_ver()[0] != 'XP' else 'http'
      os.environ['_NT_SYMBOL_PATH'] = (
          'SRV*' + symbol_dir + '*' +
          protocol + '://msdl.microsoft.com/download/symbols')

    pipe_name = r'\\.\pipe\end-to-end_%s_%s' % (
        os.getpid(), str(random.getrandbits(64)))

    crashy_dump_path = GetDumpFromCrashyProgram(args[0], pipe_name)
    if not crashy_dump_path:
      return 1

    start_handler_dump_path = GetDumpFromCrashyProgram(args[0], None)
    if not start_handler_dump_path:
      return 1

    destroyed_dump_path = GetDumpFromSelfDestroyingProgram(args[0], pipe_name)
    if not destroyed_dump_path:
      return 1

    z7_dump_path = None
    if not args[0].endswith('x64'):
      z7_dump_path = GetDumpFromZ7Program(args[0], pipe_name)
      if not z7_dump_path:
        return 1

    other_program_path = GetDumpFromOtherProgram(args[0], pipe_name)
    if not other_program_path:
      return 1

    other_program_no_exception_path = GetDumpFromOtherProgram(
        args[0], pipe_name, 'noexception')
    if not other_program_no_exception_path:
      return 1

    RunTests(cdb_path,
             crashy_dump_path,
             start_handler_dump_path,
             destroyed_dump_path,
             z7_dump_path,
             other_program_path,
             other_program_no_exception_path,
             pipe_name)

    return 1 if g_had_failures else 0
  finally:
    CleanUpTempDirs()


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))

/* vim: le=unix sw=2 : */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Songbird Distribution Stub Helper.
 *
 * The Initial Developer of the Original Code is
 * POTI <http://www.songbirdnest.com/>.
 * Portions created by the Initial Developer are Copyright (C) 2008-2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mook <mook@songbirdnest.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "error.h"
#include "stringconvert.h"
#include "debug.h"
#include "commands.h"
#include "tchar.h"

// unix include
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

// mac includes
#include <AvailabilityMacros.h>
#import <Cocoa/Cocoa.h>
#import <CoreServices/CoreServices.h>


#define MAX_LONG_PATH 0x8000 /* 32767 + 1, maximum size for \\?\ style paths */

#define NS_ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))

tstring ResolvePathName(std::string aSrc) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSString *src = [NSString stringWithUTF8String:aSrc.c_str()];
  tstring result;
  #if DEBUG
    DebugMessage("Resolving path name %s", [src UTF8String]);
  #endif
  if (0 != [src length]) {
    if ([src hasPrefix:@"$/"]) {
      NSString * appDir =
        [NSString stringWithUTF8String:GetAppDirectory().c_str()];
      src = [[appDir substringFromIndex:2] stringByAppendingString:src];
    }
    if (![src isAbsolutePath]) {
      NSString *distIniDir =
        [NSString stringWithUTF8String:GetDistIniDirectory().c_str()];
      NSString * resolved = [distIniDir stringByAppendingPathComponent:src];
      if ([[NSFileManager defaultManager] fileExistsAtPath:src]) {
        src = resolved;
      }
      else {
        DebugMessage("Failed to resolve path name %S", [src UTF8String]);
      }
    }
    result.assign([src UTF8String]);
  }
  [pool release];
  #if DEBUG
    DebugMessage("Resolved path name %s", result.c_str());
  #endif
  return result;
}

static bool isDirectoryEmpty(std::string aPath) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  NSString *path = [NSString stringWithUTF8String:aPath.c_str()];

  NSDirectoryEnumerator *enumerator =
    [[NSFileManager defaultManager] enumeratorAtPath:path];

  NSString *child = [enumerator nextObject];
  bool hasChild = (child != nil);
  [pool release];
  return !hasChild;
}

int CommandCopyFile(std::string aSrc, std::string aDest, bool aRecursive) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  NSString *srcPath = [NSString stringWithUTF8String:aSrc.c_str()],
           *destPath = [NSString stringWithUTF8String:aDest.c_str()];
  BOOL success, isDir;
  NSError *error = nil;
  NSFileManager* fileMan = [NSFileManager defaultManager];

  if (![fileMan fileExistsAtPath:srcPath isDirectory:&isDir]) {
    [pool release];
    return DH_ERROR_READ;
  }

  // XXXMook: ugly hack to check if we're using the 10.5 SDK or higher (that is,
  // a SDK that knows about 10.5 methods).
  #ifdef kCFCoreFoundationVersionNumber10_5
    if ((!aRecursive) && isDir) {
      // we don't want to recurse, but this is a directory; just create a new,
      // empty one instead
      NSDictionary* attribs = [fileMan attributesOfItemAtPath:srcPath
                                                        error:NULL];
      success = [fileMan createDirectoryAtPath:dest
                   withIntermediateDirectories:YES
                                    attributes:attribs
                                         error:NULL];
    }
    else {
      success = [fileMan copyItemAtPath:srcPath
                                 toPath:destPath
                                  error:&error];
    }
  #else
    if ((!aRecursive) && isDir) {
      // we don't want to recurse, but this is a directory; just create a new,
      // empty one instead
      NSDictionary* attribs = [fileMan fileAttributesAtPath:srcPath
                                               traverseLink:YES];
      success = [fileMan createDirectoryAtPath:destPath
                                    attributes:attribs];
    }
    else {
      success = [fileMan copyPath:srcPath
                           toPath:destPath
                          handler:nil];
    }
  #endif

  [pool release];

  return (success && !error ? DH_ERROR_OK : DH_ERROR_WRITE);
}

int CommandMoveFile(std::string aSrc, std::string aDest, bool aRecursive) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  NSString *srcPath = [NSString stringWithUTF8String:aSrc.c_str()],
           *destPath = [NSString stringWithUTF8String:aDest.c_str()];
  BOOL success, isDir;
  NSError *error = nil;
  NSFileManager* fileMan = [NSFileManager defaultManager];

  if (![fileMan fileExistsAtPath:srcPath isDirectory:&isDir]) {
    [pool release];
    return DH_ERROR_READ;
  }
  if (!aRecursive && isDir) {
    DebugMessage("We can't non-recursively move the directory %s to %s!",
                 aSrc.c_str(), aDest.c_str());
    [pool release];
    return DH_ERROR_USER;
  }

  // XXXMook: ugly hack to check if we're using the 10.5 SDK or higher (that is,
  // a SDK that knows about 10.5 methods).
  #ifdef kCFCoreFoundationVersionNumber10_5
    success = [fileMan moveItemAtPath:srcPath
                               toPath:destPath
                                error:&error];
  #else
    success = [fileMan movePath:srcPath
                         toPath:destPath
                        handler:nil];
  #endif

  [pool release];

  return (success && !error ? DH_ERROR_OK : DH_ERROR_WRITE);
}

int CommandDeleteFile(std::string aFile, bool aRecursive) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  NSString *path = [NSString stringWithUTF8String:aFile.c_str()];
  BOOL success, isDir;
  NSError *error = nil;
  NSFileManager* fileMan = [NSFileManager defaultManager];

  if (isDir && !aRecursive && !isDirectoryEmpty(aFile)) {
    LogMessage("Failed to recursively delete %s, not empty", aFile.c_str());
    [pool release];
    return DH_ERROR_WRITE;
  }

  // XXXMook: ugly hack to check if we're using the 10.5 SDK or higher (that is,
  // a SDK that knows about 10.5 methods).
  #ifdef kCFCoreFoundationVersionNumber10_5
    success = [fileMan removeItemAtPath:path
                                  error:&error];
  #else
    success = [fileMan removeFileAtPath:path
                                handler:nil];
  #endif

  [pool release];

  return (success && !error ? DH_ERROR_OK : DH_ERROR_WRITE);
}

int CommandExecuteFile(const std::string& aExecutable,
                       const std::string& aArgs) {
  tstring executable(FilterSubstitution(ConvertUTF8toUTFn(aExecutable)));
  tstring arg(FilterSubstitution(ConvertUTF8toUTFn(aArgs)));

  DebugMessage("<%s>",
               ConvertUTFnToUTF8(arg).c_str());
  int result = system(arg.c_str());
  return (result ? DH_ERROR_UNKNOWN : DH_ERROR_OK);
}

tstring FilterSubstitution(tstring aString) {
  tstring result = aString;
  tstring::size_type start = 0, end = tstring::npos;
  while (true) {
    start = result.find(tstring::value_type('$'), start);
    if (start == tstring::npos) {
      break;
    }
    end = result.find(tstring::value_type('$'), start + 1);
    if (end == tstring::npos) {
      break;
    }
    // Try to substitute $APPDIR$
    tstring variable = result.substr(start + 1, end - start - 1);
    if (variable == _T("APPDIR")) {
      tstring appdir = GetAppDirectory();
      DebugMessage("AppDir: %s", appdir.c_str());
      result.replace(start, end-start+1, appdir);
      start += appdir.length();
      continue;
    }
    // Try to substitute $XXX$ with environment variable %DISTHELPER_XXX%
    tstring envName(_T("DISTHELPER_"));
    envName.append(variable);
    tstring envValue = getenv(envName.c_str());
    if (envValue.length() > 0) {
      DebugMessage("Environment %s: %s", envName.c_str(), envValue.c_str());
      result.replace(start, end-start+1, envValue);
      start += envValue.length();
      continue;
    }
    // Try to substitute $XXX$ with environment variable %XXX%
    envValue = getenv(variable.c_str());
    if (envValue.length() > 0) {
      DebugMessage("Environment %s: %s", variable.c_str(), envValue.c_str());
      result.replace(start, end-start+1, envValue);
      start += envValue.length();
      continue;
    }
    start = end + 1;
  }
  return result;
}

std::vector<std::string> ParseCommandLine(const std::string& aCommandLine) {
  static const char WHITESPACE[] = " \t\r\n";
  std::vector<std::string> args;
  std::string::size_type prev = 0, offset;
  offset = aCommandLine.find_last_not_of(WHITESPACE);
  if (offset == std::string::npos) {
    // there's nothing that's not whitespace, don't bother
    return args;
  }
  std::string commandLine = aCommandLine.substr(0, offset + 1);
  std::string::size_type length = commandLine.length();
  do {
    prev = commandLine.find_first_not_of(WHITESPACE, prev);
    if (prev == std::string::npos) {
      // nothing left that's not whitespace
      break;
    }
    if (commandLine[prev] == '"') {
      // start of quoted param
      ++prev; // eat the quote
      offset = commandLine.find('"', prev);
      if (offset == std::string::npos) {
        // no matching end quote; assume it lasts to the end of the command
        offset = commandLine.length();
      }
    } else {
      // unquoted
      offset = commandLine.find_first_of(WHITESPACE, prev);
      if (offset == std::string::npos) {
        offset = commandLine.length();
      }
    }
    args.push_back(commandLine.substr(prev, offset - prev));
    prev = offset + 1;
  } while (prev < length);

  return args;
}

void ParseExecCommandLine(const std::string& aCommandLine,
                          std::string&       aExecutable,
                          std::string&       aArgs)
{
  // we don't need to do anything funny here; only Windows needs that
  aExecutable.clear();
  aArgs = aCommandLine;
}

tstring GetAppDirectory() {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  NSProcessInfo* process = [NSProcessInfo processInfo];
  NSString * execPath = [[process arguments] objectAtIndex:0];
  NSString * dirPath = [execPath stringByDeletingLastPathComponent];
  tstring result([dirPath UTF8String]);
  [pool release];

  #if DEBUG
    DebugMessage("Found app directory %S", result.c_str());
  #endif
  return result;
}

tstring gDistIniDirectory;
tstring GetDistIniDirectory(const TCHAR *aPath) {
  if (aPath) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    NSString * path = [NSString stringWithUTF8String:aPath];

    if (![path isAbsolutePath]) {
      // given path is not absolute; this is relative to the appdir
      NSProcessInfo* process = [NSProcessInfo processInfo];
      NSString * execPath = [[process arguments] objectAtIndex:0];
      NSString * appDir = [execPath stringByDeletingLastPathComponent];
      path = [appDir stringByAppendingPathComponent:path];
    }

    NSFileManager * fileMan = [NSFileManager defaultManager];
    // if the given file doesn't exist, bail (because there are no actions)
    if (![fileMan fileExistsAtPath:path isDirectory:NULL]) {
      DebugMessage("File %s doesn't exist, bailing", [path UTF8String]);
      [pool release];
      return tstring(_T(""));
    }

    // now remove the file name
    path = [path stringByDeletingLastPathComponent];

    #if DEBUG
      DebugMessage("found distribution path %S", path);
    #endif
    gDistIniDirectory = tstring([path UTF8String]);
    // we expect a path separator at the end of the string
    gDistIniDirectory.append(_T("/"));
    [pool release];
  }
  return gDistIniDirectory;
}

std::string GetLeafName(std::string aSrc) {
  std::string leafName;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSString *src = [NSString stringWithUTF8String:aSrc.c_str()];
  leafName.assign([[src lastPathComponent] UTF8String]);
  [pool release];
  return leafName;
}

void ShowFatalError(const char* fmt, ...) {
  tstring appIni = ResolvePathName("$/application.ini");
  tstring bakIni = ResolvePathName("$/broken.application.ini");
  unlink(bakIni.c_str());
  rename(appIni.c_str(), bakIni.c_str());

  if (_tgetenv(_T("DISTHELPER_SILENT_FAILURE"))) {
    return;
  }

  va_list args;
  TCHAR *buffer;

  // retrieve the variable arguments
  va_start(args, fmt);
  tstring msg("An application update error has occurred; please re-install "
              "the application.  Your media has not been affected.\n\n"
              "Related deatails:\n\n");
  msg.append(fmt);

  vasprintf(&buffer, fmt, args);

  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSAlert *alert = [[NSAlert alloc] init];
  [alert setMessageText:@"Update Distribution Helper"];
  [alert setInformativeText:[NSString stringWithUTF8String:buffer]];
  [alert runModal];
  [pool release];
  free(buffer);
  va_end(args);
}

/**
 * Dummy commands that don't exist on OSX
 */
int CommandSetVersionInfo(std::string aExecutable,
                          IniEntry_t& aSection)
{
  return DH_ERROR_USER;
}

int CommandSetIcon(std::string aExecutable,
                   std::string aIconFile,
                   std::string aIconName)
{
  return DH_ERROR_USER;
}

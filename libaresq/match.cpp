// File:        match.cpp
// Author:      Robert A. van Engelen, engelen@genivia.com
// Date:        August 5, 2019
// License:     The Code Project Open License (CPOL)
//              https://www.codeproject.com/info/cpol10.aspx

#include "stdafx.h"

#include <iostream>
#include <string>
#include <cctype>

// set to 1 to enable dotglob: *. ?, and [] match a . (dotfile) at the begin or after each /
#define DOTGLOB 1

// set to 1 to enable case-insensitive glob matching
#define NOCASEGLOB 1

#define CASE(c) (NOCASEGLOB ? tolower(c) : (c))

#define PATHSEP '/'

using namespace std;

// returns TRUE if text string matches gitignore-style glob pattern
bool gitignore_glob_match(const string& text, const string& glob)
{
  size_t i = 0;
  size_t j = 0;
  size_t n = text.size();
  size_t m = glob.size();
  size_t text1_backup = string::npos;
  size_t glob1_backup = string::npos;
  size_t text2_backup = string::npos;
  size_t glob2_backup = string::npos;
  bool nodot = !DOTGLOB;
  // match pathname if glob contains a / otherwise match the basename
  if (j + 1 < m && glob[j] == '/')
  {
    // if pathname starts with ./ then ignore these pairs
    while (i + 1 < n && text[i] == '.' && text[i + 1] == PATHSEP)
      i += 2;
    // if pathname starts with a / then ignore it
    if (i < n && text[i] == PATHSEP)
      i++;
    j++;
  }
  else if (glob.find('/') == string::npos)
  {
    size_t sep = text.rfind(PATHSEP);
    if (sep != string::npos)
      i = sep + 1;
  }
  while (i < n)
  {
    if (j < m)
    {
      switch (glob[j])
      {
        case '*':
          // match anything except . after /
          if (nodot && text[i] == '.')
            break;
          if (++j < m && glob[j] == '*')
          {
            // trailing ** match everything after /
            if (++j >= m)
              return true;
            // ** followed by a / match zero or more directories
            if (glob[j] != '/')
              return false;
            // new **-loop, discard *-loop
            text1_backup = string::npos;
            glob1_backup = string::npos;
            text2_backup = i;
            glob2_backup = ++j;
            continue;
          }
          // trailing * matches everything except /
          text1_backup = i;
          glob1_backup = j;
          continue;
        case '?':
          // match anything except . after /
          if (nodot && text[i] == '.')
            break;
          // match any character except /
          if (text[i] == PATHSEP)
            break;
          i++;
          j++;
          continue;
        case '[':
        {
          // match anything except . after /
          if (nodot && text[i] == '.')
            break;
          // match any character in [...] except /
          if (text[i] == PATHSEP)
            break;
          int lastchr;
          bool matched = false;
          bool reverse = j + 1 < m && (glob[j + 1] == '^' || glob[j + 1] == '!');
          // inverted character class
          if (reverse)
            j++;
          // match character class
          for (lastchr = 256; ++j < m && glob[j] != ']'; lastchr = CASE(glob[j]))
            if (lastchr < 256 && glob[j] == '-' && j + 1 < m && glob[j + 1] != ']' ?
                CASE(text[i]) <= CASE(glob[++j]) && CASE(text[i]) >= lastchr :
                CASE(text[i]) == CASE(glob[j]))
              matched = true;
          if (matched == reverse)
            break;
          i++;
          if (j < m)
            j++;
          continue;
        }
        case '\\':
          // literal match \-escaped character
          if (j + 1 < m)
            j++;
          // FALLTHROUGH
        default:
          // match the current non-NUL character
          if (CASE(glob[j]) != CASE(text[i]) && !(glob[j] == '/' && text[i] == PATHSEP))
            break;
          // do not match a . with *, ? [] after /
          nodot = !DOTGLOB && glob[j] == '/';
          i++;
          j++;
          continue;
      }
    }
    if (glob1_backup != string::npos && text[text1_backup] != PATHSEP)
    {
      // *-loop: backtrack to the last * but do not jump over /
      i = ++text1_backup;
      j = glob1_backup;
      continue;
    }
    if (glob2_backup != string::npos)
    {
      // **-loop: backtrack to the last **
      i = ++text2_backup;
      j = glob2_backup;
      continue;
    }
    return false;
  }
  // ignore trailing stars
  while (j < m && glob[j] == '*')
    j++;
  // at end of text means success if nothing else is left to match
  return j >= m;
}

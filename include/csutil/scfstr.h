/*
    Crystal Space String interface
    Copyright (C) 1999 by Brandon Ehle (Azverkan)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __CS_SCFSTR_H__
#define __CS_SCFSTR_H__

/**\file
 * Implementation for iString
 */

#include "csextern.h"

#include "csutil/csstring.h"
#include "csutil/scf_implementation.h"
#include "iutil/string.h"

/// This is a thin SCF wrapper around csString
class CS_CRYSTALSPACE_EXPORT scfString : 
  public scfImplementation1<scfString, iString>
{
  csString s;

public:

  /// Create an empty scfString object
  scfString ();

  /// Create an scfString object and reserve space for iLength characters
  scfString (size_t iLength);

  /// Copy constructor
  scfString (const iString &copy);

  /// Yet another copy constructor
  scfString (const char *copy);

  /// Destroy a scfString object
  virtual ~scfString ();

  /// Get the pointer to the internal csString.
  const csString& GetCsString () const { return s; }

  /// Get the pointer to the internal csString.
  csString& GetCsString () { return s; }

  /// Set string capacity to NewSize characters.
  virtual void SetCapacity (size_t NewSize);
  /// Get string capacity.
  virtual size_t GetCapacity() const;
  /// Set the allocation growth increment.
  virtual void SetGrowsBy(size_t);
  /// Get the allocation growth increment.
  virtual size_t GetGrowsBy() const;

  /// Truncate the string
  virtual void Truncate (size_t iPos);

  /// Set string maximal capacity to current string length.
  virtual void ShrinkBestFit ();


  /// Clear the string (so that it contains only ending 0 character).
  virtual void Empty ();

  /// Get a copy of this string
  virtual csRef<iString> Clone () const;

  /// Get a pointer to null-terminated character data.
  virtual char const* GetData () const;

  /// Query string length
  virtual size_t Length () const;

  /// Check if string is empty
  virtual bool IsEmpty () const;

  /// Get a reference to iPos'th character
  virtual char& operator [] (size_t iPos);

  /// Get the iPos'th character
  virtual char operator [] (size_t iPos) const;

  /// Set character number iPos to iChar
  virtual void SetAt (size_t iPos, char iChar);

  /// Get character at position iPos
  virtual char GetAt (size_t iPos) const;

  /// Delete iCount characters from position iPos.
  virtual void DeleteAt (size_t iPos, size_t iCount);

  /// Insert another string into this one at position iPos
  virtual void Insert (size_t iPos, iString const* iStr);

  /// Overlay another string onto a part of this string
  virtual void Overwrite (size_t iPos, iString const* iStr);

  /// Append an ASCIIZ string to this one (up to iCount characters)
  virtual void Append (const char* iStr, size_t iCount = (size_t)-1);

  /// Append a string to this one (possibly iCount characters from the string)
  virtual void Append (iString const* iStr, size_t iCount = (size_t)-1);

  /// Append a single character to this string.
  virtual void Append (char c);

  /**
   * Copy and return a portion of this string.  The substring runs from `start'
   * for `len' characters.  If 'start' and 'len' are omitted, a copy of the
   * whole string is returned.  If 'len' is omitted, a copy of the string
   * containing all characters after (and including) start is returned.
   */
  virtual csRef<iString> Slice (size_t start=0, size_t len=(size_t)-1) const;

  /**
   * Copy and return a portion of this string.  This version differs from
   * Slice() in that the 'start' parameter is counted from the END of the
   * string rather than the beginning. The substring runs from `start'
   * for `len' characters. If 'len' is omitted, a copy of the string containing
   * all characters after (and including) start is returned.
   */
  virtual csRef<iString> ReverseSlice (size_t start,
  	size_t len=(size_t)-1) const;

  /**
   * Copy a portion of this string.  The result is placed in 'sub'.  The
   * substring is from 'start', of length 'len'.  
   * If 'len' is omitted, a copy of the string    
   * containing all characters after (and including) 'start' is returned.
   */
  virtual void SubString (iString* sub, size_t start,
  	size_t len=(size_t)-1) const;

  /**
   * Copy a portion of this string.  This version differs from SubString()
   * in that the 'start' parameters is counted from the END of the string rather
   * than the beginning. The result is placed in 'sub'.  The
   * substring is from 'start', of length 'len'. 
   * If 'start' and 'len' are omitted, a copy of the whole string is returned.  
   * If 'len' is omitted, a copy of the string    
   * containing all characters after (and including) 'start' is returned.
   */
  virtual void ReverseSubString (iString* sub, size_t start=0,
  	size_t len=(size_t)-1) const;

  /**
   * Find first character 'c' from position 'p'.
   * If the character cannot be found, this function returns (size_t)-1
   */
  virtual size_t FindFirst (const char c, size_t p = (size_t)-1) const;
  /**
   * Find last character 'c', counting backwards from position 'p'.
   * Default position is the end of the string.
   * If the character cannot be found, this function returns (size_t)-1
   */
  virtual size_t FindLast (const char c, size_t p = (size_t)-1) const;

  /**
   * Find the first occurrence of \p search in this string starting at \p pos.
   * \param search String to locate.
   * \param pos Start position of search (default 0).
   * \return First position of \p search, or (size_t)-1 if not found.
   */
  virtual size_t Find (const char* search, size_t pos = 0) const;

  /**
   * Find all occurrences of \p search in this string and replace them with
   * \p replacement.
   */
  virtual void ReplaceAll (const char* search, const char* replacement);

  /**
   * Format.
   * \sa \ref FormatterNotes
   */
  virtual void Format (const char* format, ...) CS_GNUC_PRINTF (2, 3);
  /**
   * Format.
   * \sa \ref FormatterNotes
   */
  virtual void FormatV (const char* format, va_list args);

  /// Replace contents of this string with the contents of another
  virtual void Replace (const iString* iStr, size_t iCount = (size_t)-1);

  /// Replace contents of this string with the contents of another
  virtual void Replace (const char* iStr, size_t iCount = (size_t)-1);

  /// Check if two strings are equal
  virtual bool Compare (const iString* iStr) const;

  /// Compare two strings ignoring case
  virtual bool CompareNoCase (const iString* iStr) const;

  /// Check if two strings are equal
  virtual bool Compare (const char* iStr) const;

  /// Compare two strings ignoring case
  virtual bool CompareNoCase (const char* iStr) const;

  /// Check if this string starts with another
  virtual bool StartsWith (const iString* iStr, bool ignore_case = false) const;

  /// Check if this string starts with another
  virtual bool StartsWith (const char* iStr, bool ignore_case = false) const;

  /// Append another string to this
  virtual void operator += (const iString& iStr);

  /// Append a null-terminated string to this string
  virtual void operator += (const char* iStr);

  /// Append a single character to this string.
  virtual void operator += (char c);

  /// Concatenate two strings and return a third one
  virtual csRef<iString> operator + (const iString &iStr) const;

  /// Get the null-terminated C string represented by this iString.
  virtual operator char const* () const;

  /// Check if two strings are equal
  virtual bool operator == (const iString &iStr) const;

  /// Check if two strings are not equal
  virtual bool operator != (const iString &iStr) const;

  /// Convert string to lowercase.
  virtual void Downcase();

  /// Convert string to uppercase.
  virtual void Upcase();
};

#endif // __CS_SCFSTR_H__

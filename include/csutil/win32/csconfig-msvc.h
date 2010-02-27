/*
    Copyright (C) 1998-2005 by Crystal Space Development Team

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

/*
  This header is used by CS_COMPILER_MSVC and CS_COMPILER_BCC for
  CS_PLATFORM_WIN32 builds.  It is not used for CS_COMPILER_GCC builds since
  GCC builds are performed using configuration information detected by the CS
  configure script, which generates a suitable csconfig.h as output.
*/
#ifndef __CS_WIN32_CSCONFIG_H__
#define __CS_WIN32_CSCONFIG_H__

#define CS_PACKAGE_NAME "crystalspace"

#define CS_PLATFORM_WIN32
#if !defined(CS_PLATFORM_NAME)
#  define CS_PLATFORM_NAME "Win32"
#endif

#define CS_PROCESSOR_X86
#if !defined(CS_PROCESSOR_NAME)
#  define CS_PROCESSOR_NAME "X86"
#endif

#if defined(__BORLANDC__)
#  define CS_COMPILER_BCC
#  if !defined(CS_COMPILER_NAME)
#    define CS_COMPILER_NAME "Borland"
#  endif
#  define CS_USE_CUSTOM_ISDIR
#elif defined(__MINGW32__) || defined(__CYGWIN32__)
#  define CS_COMPILER_GCC
#  if !defined(CS_COMPILER_NAME)
#    define CS_COMPILER_NAME "GCC"
#  endif
#else
#  define CS_COMPILER_MSVC
#  if !defined(CS_COMPILER_NAME)
#    define CS_COMPILER_NAME "VisualC"
#  endif
#endif

#if !defined (CS_COMPILER_GCC)
#  define CS_HAVE_MMX
#endif

#undef  CS_HAVE_SOCKLEN_T
#define CS_HAVE_MATH_H_FLOAT_FUNCS
#define CS_HAVE_WCHAR_H
#define CS_HAVE_WCHAR_T
#define CS_HAVE_WCSLEN
#define CS_WCHAR_T_SIZE 2

#if defined(CS_COMPILER_BCC)
#define CS_HAVE_STDINT_H
#define CS_HAVE_INTPTR_T
#endif

#define CS_EMBED_PLUGIN_META

//#define CS_BUILD_SHARED_LIBS

#ifdef _WIN64
#  define CS_PROCESSOR_SIZE 64
#else
#  define CS_PROCESSOR_SIZE 32
#endif

#define CS_LONG_SIZE 4

// MSVC int64 format specifiers, to be used with native formatting functions
#if defined(CS_COMPILER_MSVC)
#define PRId64 "I64d"
#define PRIx64 "I64x"
#define PRIu64 "I64u"
#define PRIX64 "I64X"
#endif

// MSVC has only _finite
#if defined(CS_COMPILER_MSVC)
#define CS_HAVE__FINITE
#define CS_HAVE__ISNAN
#endif

// --- Stuff that needs to go before *any* inclusion of VC standard headers:

#if defined(CS_COMPILER_MSVC) && (_MSC_VER >= 1400)
#  ifndef _CRT_SECURE_NO_DEPRECATE
#    define _CRT_SECURE_NO_DEPRECATE
	  /* In VC8, a lot of CRT methods were marked "deprecated" b/c they're 
	   * deemed "insecure". Silence those warnings. 
	   * NB: This is here b/c it needs to be set before any standard headers
	   * are included. */
#  endif
#  ifndef _CRT_NONSTDC_NO_DEPRECATE
#    define _CRT_NONSTDC_NO_DEPRECATE /* Similar.	*/
#  endif
#endif

#if defined(CS_COMPILER_MSVC) && (CS_PROCESSOR_SIZE >= 64)
  /* x64 MSVC doesn't support MMX intrinsics - they compile fine but fail to
     link
     See e.g. http://msdn.microsoft.com/en-us/library/08x3t697.aspx */
  #ifdef CS_SUPPORTS_MMX
  #undef CS_SUPPORTS_MMX
  #endif
#endif

// Check for support of native aligned allocation
#if defined(CS_COMPILER_MSVC) && defined(_MSC_VER) && (_MSC_VER >= 1300)
  #define CS_HAVE__ALIGNED_MALLOC
#endif

#endif // __CS_WIN32_CSCONFIG_H__

#ifndef COMMON_H
#define COMMON_H

#if !defined(UNIX) && !defined(WIN32)
#error "Platform unsupported."
#elif defined(UNIX) && defined(WIN32)
#error "Incompatible build targets."
#else
#
#endif

/* Define HAVE_BCI (aka bounds-checking interfaces) if appropriate.
   Unfortunately the MSVC implementation of BCI might differ slightly
   compared to the C11 standard.  Check if a function differ before
   using this macro. */
#if defined(__STDC_LIB_EXT1__) || defined(WIN32)
#define HAVE_BCI 1

#ifdef UNIX
#define __STDC_WANT_LIB_EXT1__ 1
#endif
#endif

#if defined(UNIX) && defined(__GNUC__)
#define SW_INLINE	inline
#define SW_NORET	__attribute__((noreturn))
#elif defined(WIN32)
#define SW_INLINE	__inline
#define SW_NORET	__declspec(noreturn)
#else
#define SW_INLINE
#define SW_NORET
#endif

#define ARRAY_SIZE(ar)	(sizeof (ar) / sizeof ((ar)[0]))
#define BZERO(b, len)	((void) memset(b, 0, len))

#ifdef WIN32
#define strcasecmp	_stricmp
#define strncasecmp	_strnicmp
#define strtok_r	strtok_s
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#endif

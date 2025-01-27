/* Copyright (C) 2003 Free Software Foundation, Inc.
   This file is part of the GNU CHARSET Library.

   The GNU CHARSET Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU CHARSET Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with the GNU CHARSET Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef _LIBCHARSET_H
#define _LIBCHARSET_H

#define LIBCHARSET_STATIC

#ifdef LIBCHARSET_STATIC
#define LIBCHARSET_DLL_EXPORTED 
#else /* LIBCHARSET_STATIC */
#ifdef BUILDING_LIBCHARSET
#define LIBCHARSET_DLL_EXPORTED __declspec(dllexport)
#else
#define LIBCHARSET_DLL_EXPORTED __declspec(dllimport)
#endif
#endif /* LIBCHARSET_STATIC */

#include <localcharset.h>


#ifdef __cplusplus
extern "C" {
#endif


/* Support for relocatable packages.  */

/* Sets the original and the current installation prefix of the package.
   Relocation simply replaces a pathname starting with the original prefix
   by the corresponding pathname with the current prefix instead.  Both
   prefixes should be directory names without trailing slash (i.e. use ""
   instead of "/").  */
extern LIBCHARSET_DLL_EXPORTED void libcharset_set_relocation_prefix (const char *orig_prefix,
					      const char *curr_prefix);


#ifdef __cplusplus
}
#endif


#endif /* _LIBCHARSET_H */

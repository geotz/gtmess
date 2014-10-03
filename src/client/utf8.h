/*
 *    utf8.h
 *
 *    utf8 support routines
 *    Copyright (C) 2002-2007  George M. Tzoumas
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _UTF8_H_
#define _UTF8_H_

#include <wchar.h>
#include "../config.h"

int strlen_utf8(char *s, int *len);
int strnlen_utf8(char *s, int n, int *len);
int stroffset(char *s, int n, int len);
int seqlen(int c);
int strwidth(char *s);
int widthoffset(char *s, int width);
int wstrwidth(const wchar_t *s, size_t n);

#ifndef HAVE_WCWIDTH
#define wcwidth(c) (1)
#endif

#define wcwidth_nl(c) ((c) == '\n'? 1: wcwidth(c))

#ifndef HAVE_MBSRTOWCS

#define mbsrtowcs(dest, src, len, ps) mbstowcs(dest, *(src), len)
#define wcsrtombs(dest, src, len, ps) wcstombs(dest, *(src), len)
#define wcrtomb(s, wc, ps) wctomb(s, wc)
#define mbrtowc(pwc, s, n, ps) mbtowc(pwc, s, n)

#endif

#endif

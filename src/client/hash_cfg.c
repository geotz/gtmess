/* ANSI-C code produced by gperf version 3.0.4 */
/* Command-line: gperf -t -LANSI-C hash_cfg.inp  */
/* Computed positions: -k'3,5' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gnu-gperf@gnu.org>."
#endif

#line 1 "hash_cfg.inp"

/*
 *    hash_cfg.c
 *
 *    gtmess - MSN Messenger client
 *    Copyright (C) 2002-2011  George M. Tzoumas
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
 
#include <string.h>
#line 25 "hash_cfg.inp"
struct hct_entry {
    char name[256];
    int id; 
};

#define TOTAL_KEYWORDS 42
#define MIN_WORD_LENGTH 3
#define MAX_WORD_LENGTH 20
#define MIN_HASH_VALUE 3
#define MAX_HASH_VALUE 71
/* maximum key range = 69, duplicates = 0 */

#if (defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || defined(__cplusplus) || defined(__GNUC_STDC_INLINE__)
inline
#elif defined(__GNUC__)
__inline
#endif
static unsigned int
hash (register const char *str, register unsigned int len)
{
  static unsigned char asso_values[] =
    {
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72,  5, 72, 72, 72, 40,
      15,  0, 25, 20, 72, 10, 72, 72, 20, 15,
       0, 25,  0, 72,  0, 10,  5, 20,  0, 30,
      35, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72, 72, 72, 72, 72,
      72, 72, 72, 72, 72, 72
    };
  register int hval = len;

  switch (hval)
    {
      default:
        hval += asso_values[(unsigned char)str[4]];
      /*FALLTHROUGH*/
      case 4:
      case 3:
        hval += asso_values[(unsigned char)str[2]];
        break;
    }
  return hval;
}

#ifdef __GNUC__
__inline
#if defined __GNUC_STDC_INLINE__ || defined __GNUC_GNU_INLINE__
__attribute__ ((__gnu_inline__))
#endif
#endif
struct hct_entry *
in_word_set (register const char *str, register unsigned int len)
{
  static struct hct_entry wordlist[] =
    {
      {""}, {""}, {""},
#line 35 "hash_cfg.inp"
      {"cvr", 5},
      {""},
#line 33 "hash_cfg.inp"
      {"popup", 3},
#line 39 "hash_cfg.inp"
      {"server", 9},
      {""},
#line 70 "hash_cfg.inp"
      {"pop_exec", 40},
      {""},
#line 66 "hash_cfg.inp"
      {"force_nick", 36},
      {""},
#line 45 "hash_cfg.inp"
      {"msnftpd", 15},
      {""},
#line 51 "hash_cfg.inp"
      {"invitable", 21},
#line 60 "hash_cfg.inp"
      {"keep_alive", 30},
#line 36 "hash_cfg.inp"
      {"cert_prompt", 6},
#line 65 "hash_cfg.inp"
      {"auto_cl", 35},
#line 68 "hash_cfg.inp"
      {"psm", 38},
      {""},
#line 50 "hash_cfg.inp"
      {"auto_login", 20},
#line 61 "hash_cfg.inp"
      {"nonotif_mystatus", 31},
#line 67 "hash_cfg.inp"
      {"passp_server", 37},
#line 58 "hash_cfg.inp"
      {"snd_exec", 28},
#line 62 "hash_cfg.inp"
      {"skip_says", 32},
#line 40 "hash_cfg.inp"
      {"login", 10},
#line 31 "hash_cfg.inp"
      {"colors", 1},
#line 46 "hash_cfg.inp"
      {"aliases", 16},
#line 59 "hash_cfg.inp"
      {"url_exec", 29},
      {""},
#line 48 "hash_cfg.inp"
      {"msg_notify", 18},
#line 43 "hash_cfg.inp"
      {"online_only", 13},
#line 56 "hash_cfg.inp"
      {"update_nicks", 26},
#line 49 "hash_cfg.inp"
      {"idle_sec", 19},
#line 42 "hash_cfg.inp"
      {"initial_status", 12},
#line 34 "hash_cfg.inp"
      {"time_user_types", 4},
#line 30 "hash_cfg.inp"
      {"log_traffic", 0},
#line 57 "hash_cfg.inp"
      {"snd_dir", 27},
#line 63 "hash_cfg.inp"
      {"safe_msg", 33},
      {""},
#line 32 "hash_cfg.inp"
      {"sound", 2},
#line 38 "hash_cfg.inp"
      {"console_encoding", 8},
#line 71 "hash_cfg.inp"
      {"safe_history", 41},
#line 55 "hash_cfg.inp"
      {"notif_aliases", 25},
#line 47 "hash_cfg.inp"
      {"msg_debug", 17},
#line 52 "hash_cfg.inp"
      {"gtmesscontrol_ignore", 22},
#line 69 "hash_cfg.inp"
      {"max_retries", 39},
#line 53 "hash_cfg.inp"
      {"max_nick_len", 23},
#line 41 "hash_cfg.inp"
      {"password", 11},
#line 44 "hash_cfg.inp"
      {"syn_cache", 14},
      {""}, {""}, {""},
#line 64 "hash_cfg.inp"
      {"err_connreset", 34},
      {""}, {""}, {""}, {""},
#line 37 "hash_cfg.inp"
      {"common_name_prompt", 7},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""}, {""}, {""},
#line 54 "hash_cfg.inp"
      {"log_console", 24}
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register int key = hash (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
        {
          register const char *s = wordlist[key].name;

          if (*str == *s && !strcmp (str + 1, s + 1))
            return &wordlist[key];
        }
    }
  return 0;
}

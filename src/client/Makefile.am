#    gtmess - MSN Messenger client
#    Copyright (C) 2002-2006  George M. Tzoumas
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#    This file must be processed by automake

AUTOMAKE_OPTIONS = gnu
bin_PROGRAMS = gtmess
AM_CFLAGS = -DDATADIR=\"${pkgdatadir}\"
EXTRA_DIST = hash_cfg.inp
 
gtmess_SOURCES = gtmess.c editbox.c hash_cfg.c hash_tbl.c hlist.c md5.c menu.c msn.c nserver.c\
                pass.c queue.c sound.c sboard.c screen.c unotif.c utf8.c util.c xfer.c\
                editbox.h gtmess.h hash_cfg.h hash_tbl.h hlist.h md5.h menu.h msn.h\
                nserver.h pass.h queue.h sound.h sboard.h screen.h unotif.h utf8.h util.h xfer.h

#gtmess_LDFLAGS = -L../inty
gtmess_LDADD = ../inty/libinty.a

hash_cfg.c: hash_cfg.inp
	gperf -t -LANSI-C hash_cfg.inp > hash_cfg.c

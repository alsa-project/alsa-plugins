/**
 * @file debug.h 
 * @brief Debugging Macros
 * <p>
 * Copyright (C) 2006 Nokia Corporation
 * <p>
 * Contact: Eduardo Bezerra Valentin <eduardo.valentin@indt.org.br>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 * */
#ifndef _DEBUG_H
#define _DEBUG_H

//#define DEBUG
#undef DEBUG
#ifdef DEBUG
#define DEBUG_OUTPUT stderr
#define DPRINT(fmt,arg...) fprintf(DEBUG_OUTPUT, "%s(): " fmt,\
	       	__FUNCTION__, ##arg)
#define DENTER()  fprintf(DEBUG_OUTPUT, "ENTER %s()\n", __FUNCTION__)
#define DLEAVE(a)  fprintf(DEBUG_OUTPUT, "LEAVE %s() %d\n", __FUNCTION__, a)
#else
#define DPRINT(fmt,arg...) do { } while (0)
#define DENTER() do { } while (0)
#define DLEAVE(a) do { } while (0)
#endif

/* Errors on/off */
#define ERROR_DEBUG
/* #undef ERROR_DEBUG */
#ifdef ERROR_DEBUG
#define DERROR(fmt,arg...) 	fprintf(stderr, "%s(): " fmt, \
		__FUNCTION__, ##arg)
#else
#define DERROR(fmt,arg...) \
	do { } while (0)
#endif

#endif				/* _DEBUG_H */

/*
 * The ARAnyM MetaDOS driver.
 *
 * 2002 STan
 *
 * Based on:
 * debug.h,v 1.2 2001/06/13 20:21:42 fna Exp
 *
 * This file has been modified as part of the FreeMiNT project. See
 * the file Changes.MH for details and dates.
 */

/*
 * Copyright 1991,1992 Eric R. Smith.
 * Copyright 1992,1993,1994 Atari Corporation.
 * All rights reserved.
 */

# ifndef _mint_debug_h
# define _mint_debug_h


extern int debug_level;
extern int out_device;
extern int debug_logging;

/*
 * Extra terse settings - don't even output ALERTs unless asked to.
 *
 * Things that happen in on an idle Desktop are at LOW_LEVEL:
 * Psemaphore, Pmsg, Syield.
 */

# define FORCE_LEVEL	0
# define ALERT_LEVEL	1
# define DEBUG_LEVEL	2
# define TRACE_LEVEL	3
# define LOW_LEVEL	4


# ifndef DEBUG_INFO

# define TRACELOW(x)
# define TRACE(x)
# define DEBUG(x)

# else

# define TRACELOW(s)	\
	do { if ((debug_level >=   LOW_LEVEL) || debug_logging) Tracelow s ; } while (0)
# define TRACE(s)	\
	do { if ((debug_level >= TRACE_LEVEL) || debug_logging) Trace s ;    } while (0)
# define DEBUG(s)	\
	do { if ((debug_level >= DEBUG_LEVEL) || debug_logging) Debug s ;    } while (0)

# endif /* DEBUG_INFO */


void		debug_ws	(const char *s);

void	_cdecl	Tracelow	(const char *s, ...);
void	_cdecl	Trace		(const char *s, ...);
void	_cdecl	Debug		(const char *s, ...);
void	_cdecl	ALERT		(const char *s, ...);

EXITING	_cdecl	FATAL		(const char *s, ...)	NORETURN;
EXITING		halt		(void)			NORETURN;
EXITING		HALT		(void)			NORETURN;

void		DUMPLOG		(void);


# endif /* _mint_debug_h */

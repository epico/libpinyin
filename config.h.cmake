/*--------------------------------------------------------------------------
 * This file is autogenerated from config.h.in 
 * during the cmake configuration of your project. If you need to make changes
 * edit the original file NOT THIS FILE.
 * --------------------------------------------------------------------------*/
#ifndef _CONFIGURATION_HEADER_GUARD_H_
#define _CONFIGURATION_HEADER_GUARD_H_

/* Define to 1 if you have a working `mmap' system call. */
#cmakedefine HAVE_MMAP @HAVE_MMAP@

/* Have Berkeley DB. */
#cmakedefine HAVE_BERKELEY_DB @HAVE_BERKELEY_DB@

/* Have Kyoto Cabinet. */
#cmakedefine HAVE_KYOTO_CABINET @HAVE_KYOTO_CABINET@

/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif

#endif

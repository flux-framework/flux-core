/* mp_util. - exercise public interfaces */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <netdb.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>

#define COBOOUT_PREFIX        "MP_COBO"
#define COBOFEN_MAX_STR_LEN   PATH_MAX 
#define COBOFEN_MAXHL         8000
#define COBOFEN_PORT_RANGE    256
#define COBOFEN_BASE_PORT     58950
#define COBOFEN_SESSION       10313


double gettimeofdayD ();

extern void
mp_cobo_say_msg (const char *m, int error_or_info, 
		 const char *output, ... );


extern int 
(*errorCB) (const char *format, va_list ap);

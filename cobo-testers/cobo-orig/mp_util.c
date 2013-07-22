
/* mp_util.c - exercise public interfaces */
 
#include "mp_util.h"


int (*errorCB) (const char *format, va_list ap) = NULL;


double 
gettimeofdayD ()
{
    struct timeval ts;
    double rt;
    gettimeofday (&ts, NULL);
    rt = (double) (ts.tv_sec);
    rt += (double) ((double)(ts.tv_usec))/1000000.0;
    return rt;
} 


static int
mp_cobo_timestamp (const char *m, const char *ei, 
		   const char *fstr, char *obuf, uint32_t len)
{
  char timelog[PATH_MAX];
  const char *format = "%b %d %T";
  time_t t;
  int rc;

  time(&t);
  strftime (timelog, PATH_MAX, format, localtime(&t));
  rc = snprintf(obuf, len, "<%s> %s (%s): %s\n", 
		timelog, m, ei, fstr);
  return rc;
}


void
mp_cobo_say_msg (const char *m, int error_or_info, 
		 const char *output, ... )
{
  va_list ap;
  char log[PATH_MAX];
  const char *ei_str = error_or_info ? "ERROR" : "INFO";
 
  if (mp_cobo_timestamp(m, ei_str, output, log, PATH_MAX) >= 0) {
      va_start(ap, output);
      if (errorCB) {
          errorCB (log, ap);
      }
      else {
          vfprintf(stdout, log, ap);
          fflush(stdout);	
      }
      va_end(ap);
  }
}

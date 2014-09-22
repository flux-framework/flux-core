#include <sched.h>

#ifndef _HAVE_CPUSET_STR_H
#define _HAVE_CPUSET_STR_H

int hex_to_cpuset (cpu_set_t *mask, const char *str);
int cpuset_to_hex (cpu_set_t *mask, char *str, size_t len, char *sep);
int cstr_to_cpuset(cpu_set_t *mask, const char* str);
int str_to_cpuset(cpu_set_t *mask, const char* str);
char * cpuset_to_cstr (cpu_set_t *mask, char *str);

#endif /* !_HAVE_CPUSET_STR_H */

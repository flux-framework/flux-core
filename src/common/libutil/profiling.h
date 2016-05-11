#ifndef __UTIL_PROFILING_H
#define __UTIL_PROFILING_H

#if HAVE_CONFIG_H
#include <config.h>
#endif

// #define TAU_PROFILING
#if defined(TAU_PROFILING)

// #define PROFILING_ON
#include <TAU.h>

#define PROFILING_INIT(X,Y) TAU_INIT(X, Y)
#define PROFILING_SET_RANK(X) TAU_PROFILE_SET_NODE(X)
#define PROFILING_REGION_START(X) TAU_START(X)
#define PROFILING_REGION_STOP(X) TAU_STOP(X)

#elif defined(CALIPER_PROFILING)

#include <caliper/cali.h>

#define PROFILING_INIT(X,Y) cali_begin_byname((Y)[0][0])
#define PROFILING_SET_RANK(X) cali_begin_int_byname("rank", X)
#define PROFILING_REGION_START(X) cali_begin_byname(X)
#define PROFILING_REGION_STOP(X) cali_end_byname(X)

#else

void profiling_init (int *argc, char **argv[]);
#define PROFILING_INIT(X,Y) profiling_init (X, Y)
#define PROFILING_SET_RANK(X)
#define PROFILING_REGION_START(X)
#define PROFILING_REGION_STOP(X)

#endif

#endif /* __UTIL_PROFILING_H */

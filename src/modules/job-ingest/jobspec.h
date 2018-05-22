#ifndef _FLUX_JOBSPEC_WRAP_H
#define _FLUX_JOBSPEC_WRAP_H

#ifdef __cplusplus
extern "C" {
#endif

int jobspec_validate (const char *buf, int len,
                      char *errbuf, int errbufsz);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_JOBSPEC_WRAP_H */

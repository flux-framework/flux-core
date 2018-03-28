#ifndef _FLUX_CORE_WRECK_JOBPATH_H
#define _FLUX_CORE_WRECK_JOBPATH_H

#define WRECK_MAX_JOB_PATH 1024

char *wreck_id_to_path (flux_t *h, char *buf, int bufsz, uint64_t id);

#endif /* !_FLUX_CORE_WRECK_JOBPATH_H */

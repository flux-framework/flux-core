/*
 *-------------------------------------------------------------------------------
 * Copyright and authorship blurb here
 *-------------------------------------------------------------------------------
 * scheduler.h - common data structure for scheduler framework and plugins
 *
 * Update Log:
 * 	     May 24 2012 DHA: File created.
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H 1

#include <stdint.h>
#include <czmq.h>


/**
 *  Enumerates lightweight-job and resource events
 */
typedef enum {
    j_null,      /*!< the state has yet to be assigned */
    j_reserved,  /*!< a job has a reservation in KVS*/
    j_submitted, /*!< a job added to KVS */
    j_unsched,   /*!< a job never gone through sched_loop */
    j_pending,   /*!< a job set to pending */
    j_allocated, /*!< a job got allocated to resource */
    j_runrequest,/*!< a job is requested to be executed */
    j_starting,  /*!< a job is starting */
    j_running,   /*!< a job is running */
    j_cancelled, /*!< a job got cancelled */
    j_complete,  /*!< a job completed */
    j_reaped,    /*!< a job reaped */
    j_for_rent   /*!< Space For Rent */
} lwj_event_e;


typedef lwj_event_e lwj_state_e;


/**
 *  Enumerates resource events
 */
typedef enum {
    r_null,      /*!< the state has yet to be assigned */
    r_added,     /*!< RDL reported some resources added */
    r_released,  /*!< a lwj released some resources */
    r_attempt,   /*!< attemp to schedule again */
    r_failed,    /*!< some resource failed */
    r_recovered, /*!< some failed resources recovered */
    r_for_rent   /*!< Space For Rent */
} res_event_e;


/**
 *  Whether an event is a lwj or resource event
 */
typedef enum {
    lwj_event,   /*!< is a lwj event */
    res_event,   /*!< is a resource event */
} event_class_e;

/**
 *  Defines resource request info block.
 *  This needs to be expanded as RDL evolves.
 */
typedef struct flux_resources {
    uint64_t nnodes; /*!< num of nodes requested by a job */
    uint32_t ncores; /*!< num of cores requested by a job */
} flux_res_t;


/**
 *  Defines LWJ info block (this needs to be expanded of course)
 */
typedef struct {
    int64_t lwj_id; /*!< LWJ id */
    lwj_state_e state;
    bool reserve;
    flux_res_t *req;   /*!< resources requested by this LWJ */
    struct rdl *rdl;  /*!< resources allocated to this LWJ */
} flux_lwj_t;


/**
 *  Defines an event that goes into the event queue
 */
typedef struct {
    event_class_e t;    /*!< lwj or res event? */
    union u {
        lwj_event_e je; /*!< use this if the class is lwj */
        res_event_e re; /*!< use this if the class is res */
    } ev;               /*!< event */
    flux_lwj_t *lwj;    /*!< if lwj event, a ref. Don't free */
} flux_event_t;

#endif /* SCHEDULER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */


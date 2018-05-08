Job Status and Control Application Programming Interface
===================

The Job Status and Control (JSC) API is a high-level abstraction that
allows its client software to monitor and control the status of Flux
jobs. It is designed to expose the job status and control abstraction
in a way to hide the underlying data layout of job information stored
within Flux's KVS data store. We expect that schedulers and runtime
tools  will be its main users.  This abstraction provides the producers
of job information including a task and program execution service
module such as `flux-wreckrun` with an opportunity to change and
optimize the data layout of jobs within the KVS without presenting
major impacts to the implementation of the schedulers and runtime tools.

1. Design Consideration
-------------
The main design considerations are the following:
>1.  Abstract out implementation-dependent job states;
>2.  Provide flexible and easily extensible mechanisms to pass job
information;
>3.  Use a minimalistic API set.

The first consideration has led us to use a C enumerator (i.e.,
*job\_state\_t*) to capture the job states. However, because Flux has
not yet defined its job schema, the second consideration discouraged us
to use a C user-defined type to pass job information with the client
software. Instead, JSC uses an JSON to capture the job information and
introduce the notion of Job Control Block (JCB) to have a structure on
this information. We will try to keep backward compatibility on JCB's
structure as we will extend this to keep abreast of the evolution of
Flux's job schema. Finally, the third consideration has led us to
introduce three simple API calls as the main JSC idioms: job
status-change notification as well as JCB query and update. Client
software can use the notification call to get the status of a job
asynchronously on a state change; the query and update calls to fetch
and update a job's JCB, respectively.

2. Job States
-------------
The JSC API converts the state strings produced by `flux-wreckrun` and
the scheduler framework service comms module into a C enumerator:
*job\_state\_t*. Its elements are shown in Table 2-1. If the raw state
strings are changed in the future, one must accommodate this JCB
implementation accordingly--mapping the new strings to these state
elements. We expect the state set will further be extended as we will
add more advanced services such as elastic job and resource management
service.

| Elements     | Comment |
|--------------|------------------------------------------------------|
| J_NULL       | No state has been assigned |
| J_RESERVED   | Reserved in KVS |
| J_SUBMITTED  | Added to KVS |
| J_PENDING    | Pending |
| J_SCHEDREQ   | Resource selection requested |
| J_ALLOCATED  | Resource allocated/contained in the Flux instance |
| J_RUNREQUEST | Requested to be executed |
| J_STARTING   | Starting |
| J_STOPPED    | Stopped  |
| J_RUNNING    | Running |
| J_CANCELLED  | Cancelled |
| J_COMPLETE   | Complete |
| J_REAPED     | Reaped to the upper-level Flux instance |
| J_FAILED     | Failed before running |
| J_FOR\_RENT   | To be extended |

**Table 2-1** Job state elements


| Key        | Macro          | Value Type     | Comment                                                                        |
|------------|----------------|----------------|--------------------------------------------------------------------------------|
| jobid      | JSC_JOBID      | 64-bit integer | Job id                                                                         |
| state-pair | JSC_STATE\_PAIR| dictionary     | A dictionary containing this old and new states of the job. See Table 3-2.     |
| rdesc      | JSC_RDESC      | dictionary     | Information on the resources owned by this job. See Table 3-3.                 |
| rdl        | JSC_RDL        | string         | RDL binary string allocated to the job                                         |
| R_lite     | JSC_R\_LITE    | string         | R\_lite serialized JSON string allocated to the job                            |
| pdesc      | JSC_PDESC      | dictionary     | Information on the processes spawned by this job. See Table 3-5.               |

**Table 3-1** Keys and values of top-level JCB attributes


| Key        | Macro                   | Value Type     | Comment                               |
|------------|-------------------------|----------------|---------------------------------------|
| ostate     | JSC_STATE\_PAIR\_OSTATE | 64-bit integer | Old state (a *job\_state\_t* element) |
| nstate     | JSC_STATE\_PAIR\_NSTATE | 64-bit integer | New state (a *job\_state\_t* element) |

**Table 3-2** Keys and values of  *state-pair* attribute


| Key        | Macro            | Value Type      | Comment       |
|------------|------------------|-----------------|---------------|
| nnodes     | JSC_RDESC\_NNODES | 64-bit integer | Node count    |
| ntasks     | JSC_RDESC\_NTASKS | 64-bit integer | Process count |
| ncores     | JSC_RDESC\_NCORES | 64-bit integer | core count |
| ngpus      | JSC_RDESC\_NGPUS | 64-bit integer | GPU count |
| walltime   | JSC_RDESC\_WALLTIME | 64-bit integer | Walltime    |

**Table 3-3** Keys and values of *rdesc* attribute



| Key        | Macro                | Value Type                  | Comment                                                             |
|------------|----------------------|-----------------------------|---------------------------------------------------------------------|
| procsize   | JSC_PDESC\_SIZE      | 64-bit integer              | Process count                                                       |
| hostnames  | JSC_PDESC\_HOSTNAMES | array of strings            | Host name array (Names are current home broker rank)                |
| executables| JSC_PDESC\_EXECS     | array of strings            | Executable name array                                               |
| pdarray    | JSC_PDESC\_PDARRAY   | array of dictionary objects | Process descriptor array (MPI rank order). See Table 3-6 for each pdarray element |

**Table 3-5** Keys and values of *pdesc* attribute


| Key        | Macro                           | Value Type     | Comment                                                |
|------------|---------------------------------|----------------|--------------------------------------------------------|
| pid        | JSC_PDESC\_RANK\_PDARRAY\_PID   | 64-bit integer | Process count                                          |
| hindx      | JSC_PDESC\_RANK\_PDARRAY\_HINDX | 64-bit integer | Host name (indexing into the hostname array)           |
| eindx      | JSC_PDESC\_RANK\_PDARRAY\_EINDX | 64-bit integer | Executable name (indexing into the executable name array) |

**Table 3-6** Keys and values of each *pdarray* element


3. Job Control Block
-------------
Job Control Block (JCB) is our data schema containing the information
needed to manage a particular job. It contains information such as
jobid, resources owned by the job, as well as the processes spawned by
the job.  The JSC API converts the raw information on a job into an
JCB, implemented as an JSON dictionary object. Our current JCB
structure is shown in Table 3-1 through 3-6. As Flux's job schema
evolves, we will extend JCB while trying our best to keep backward
compatibility.


4. Application Programming Interface
-------------
The Job Status and Control API currently provides three main calls:

> Note: typedef int (\*jsc\_handler\_f)(json\_object \*base\_jcb, void
\*arg, int
errnum);
>
>- int jsc\_notify\_status (flux\_t h, jsc\_handler\_f callback, void
\*d);
>- int jsc\_query\_jcb (flux\_t h, int64\_t jobid, const char \*key,
char
\*\*jcb);
>- int jsc\_update\_jcb (flux\_t h, int64\_t jobid, const char \*key,
const char
\*jcb);



#### jsc\_notify\_status
Register a *callback* to the asynchronous status change notification
service. *callback* will be invoked when the state of a job changes.
The *jobid* and *state-pair* as shown in Table 3-2 will be passed as
*base_jcb* into the *callback*. *d* is arbitrary data that will
transparently be passed into *callback*. However, one should pass its
*flux_t* object as part of this callback data. Note that the caller
must start its reactor to get an asynchronous status change
notification via *callback*. This is because it uses the KVS-watch
facility which has the same limitation.  One can register multiple
callbacks by calling this function multiple times. The callbacks will
be invoked in the order they are registered. Returns 0 on success;
otherwise -1.


#### jsc\_query\_jcb
Query the *key* attribute of JCB of *jobid*. The JCB info on this
attribute will be passed via *jcb*. It is the responsibility of the
caller to release *jcb*. All ownership associated with the
sub-attributes in *jcb*'s hierarchy are transferred to *jcb*, so that
json_object_put (\*jcb) will free this hierarchy in its entirety.
Returns 0 on success; otherwise -1.

#### jsc\_update\_jcb
Update the *key* attribute within the JCB of *jobid*. The top-level
attribute of *jcb* should be the same as *key*. Returns 0 on success;
otherwise -1. This will not release *jcb* so it is the responsibility
of the caller to free *jcb*.

>**Notes:**

>1. JCB granularity optimization -- one can optimize the amounts of JCB
information piggybacked with each notification (*base_jcb*). One can
further extend the single attribute-wise query/update pattern to
group-wise ones once the access patterns of JCS API's clients are
known.

5. Testing
-------------

To facilitate the testing of this API, we created an utility command:
`flux-jstat`.  Its usage is the following:

     Usage: flux-jstat notify
            flux-jstat query jobid <top level JCB attribute>
            flux-jstat update jobid <top level JCB attribute> <JCB JSON>

Further, `flux-core/t/t2001-jsc.t` contains various test cases that use
this utility.


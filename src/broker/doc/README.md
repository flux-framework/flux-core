## Broker Design notes

### Bootstrap

Each broker must determine its place in the Flux instance:  rank,
size, and URI of TBON parent.  It may determine this by reading
a static set of config files, or using the Process Management Interface (PMI).

### PMI

When Flux is launched by Flux, by another resource manager, or by
`flux start [--bootstrap=selfpmi] ...`, PMI provides the broker rank and
size straight away, and the PMI KVS is used to share broker URIs via
global exchange.

Each broker:
1) binds to TCP connection, claiming a random port number,
2) writes the URI to the PMI KVS using its rank as the key,
3) executes PMI barrier,
4) calculates the rank of its TBON parent from rank and branching factor,
5) reads the parent URI from PMI KVS using the parent rank as the key.

#### Config File

When Flux is launched by systemd, a TOML array of host entries is consulted.
The identical configuration is assumed to be replicated across the cluster.

Each broker:
1) locates its own entry by matching its hostname,
2) determines size and rank from array size and entry index,
3) binds to the URI specified in its entry,
4) calculates the rank of its TBON parent from rank and branching factor,
5) reads the parent URI from the array entry at parent rank index

### State Machine

After bootstrap, each broker comprising a Flux instance begins executing an
identical state machine.  Although there is synchronization between brokers
in some states, there is no distributed agreement on a global state for the
instance.

_Events_ drive the state machine.

Entry to a new state triggers an _action_.

Actions may differ across broker ranks.  For example, entering CLEANUP state
on rank 0 launches a process and an event is generated upon process termination,
while on rank > 0, entering CLEANUP does not launch a process, and immediately
generates an event.

![broker state machine picture](states.png)

#### States

**abbrev**	| **state**	| **action when transitioning into state**
:--		| :--		| :--
J		| JOIN		| connect with parent (rank > 0)
1		| INIT		| run rc1 script
2		| RUN		| run initial program (rank 0)
C		| CLEANUP	| run cleanup (rank 0)
S		| SHUTDOWN	| publish shutdown event (rank 0), wait for children to disconnect (interior ranks)
3		| FINISH	| run rc3 script
E		| EXIT		| exit broker

### Normal State Transitions

It may be helpful to walk through the state transitions that occur when
a Flux instance runs to completion without encountering exceptional conditions.

![broker state machine picture - normal](states_norm.png)

green = common path; blue = rank 0 deviation; red = leaf node deviation

#### startup

The broker ranks > 0 wait for the parent to enter RUN state (_parent-ready_)
then enter INIT state.  Rank 0 immediately enters INIT (_parent-none_).
Upon enering INIT, the rc1 script is executed, then on completion, RUN state
is entered (_rc1-success_).  Because each TBON tree level waits for the
upstream level to enter RUN state before entering INIT state, rc1 executes
in upstream-to-downstream order.  This ensures upstream service leaders are
loaded before downstream followers.

Note that the rank 0 broker does not wait for any other brokers to enter
RUN state before starting the initial program.  As a consequence, all
resources should not be expected to be online when the initial program
begins executing.

All ranks remain in RUN state until the initial program completes.

#### shutdown

When the initial program completes, rank 0 transitions to CLEANUP state
(_rc2-success_) and runs any cleanup script(s).  Cleanups execute while the
other broker ranks remain in RUN state.  Upon completion of cleanups, rank 0
enters SHUTDOWN state (_cleanup-success_).  This initiates distributed
shutdown by publishing a shutdown message.

The broker ranks > 0, on the other hand, subscribe to the shutdown message
and leave RUN upon receipt (_shutdown-abort_).  They transition through
CLEANUP (_cleanup-none_) to SHUTDOWN state.

All brokers with children remain in SHUTDOWN until their children disconnect
(_children-complete_).  If they have no children (leaf node), they transition
out of SHUTDOWN immediately (_children-none_). The next state is FINISH,
where the rc3 script is executed.  Upon completion of rc3 (_rc3-success_),
brokers transition to EXIT and disconnect from the parent.

Because each TBON tree level waits for the downstream level to disconnect
before entering FINISH state, rc3 executes in downstream-to-upstream order.
This ensures downstream service followers are unloaded before upstream leaders.

The rank 0 broker is the last to exit.

#### variation: no rc2 script (initial program)

A system instance does not define an initial program.  In this case, the
rank 0 broker transitions through the RUN state like other ranks.  Instead
of publishing the shutdown message when rc2 completes, rank 0 subscribes
to the shutdown message and remains in RUN until it transitions out with
_shutdown-abort_.

The instance runs until it is administratively shut down.  The shutdown
message is published by the `flux admin shutdown` command.

#### variation: no rc1, rc3, or cleanup scripts

In test sometimes we eliminate the rc1, cleanup, and/or rc3 scripts to simplify
or speed up a test environment.  In these cases, entry into INIT, CLEANUP,
and FINALIZE states generates a _rc1-none_, _cleanup-none_, or _rc3-none_ event,
which causes an immediate transition to the next state.

### Events

**event**	| **description**
:--		| :--
_parent-ready_	| parent has entered RUN state
_parent-none_	| this broker has no parent
_parent-fail_	| parent has ended communication with this broker
_parent-timeout_ | parent has not responded within timeout period
_rc1-none_	| rc1 script is defined on this broker
_rc1-success_	| rc1 script completed successfully
_rc1-fail_	| rc1 script completed with errors
_rc2-none_	| no rc2 script (initial program) is defined on this broker
_rc2-success_	| rc2 script completed successfully
_rc2-fail_	| rc2 script completed with errors
_shutdown-abort_ | broker received shutdown event
_signal-abort_	| broker received terminating signal
_cleanup-none_	| no cleanup script is defined on this broker
_cleanup-success_ | cleanup script completed successfully
_cleanup-fail_	| cleanup script completed with errors
_children-complete_ | all children have disconnected from this broker
_children-none_ | this broker has no children
_children-timeout_ | children did not disconnected within timeout period
_rc3-none_	| no rc3 script is defined on this broker
_rc3-success_	| rc3 script completed successfully
_rc3-fail_	| rc3 script completed with errors


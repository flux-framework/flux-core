## System Instance Resiliency

The Flux system instance has to deal with the usual challenges faced by cluster
system software, such as node crashes and network outages.  Although Flux's
design attempts to meet these challenges with minimal human intervention and
lost work, there are caveats that need to be understood by Flux developers.
This page describes Flux's current design for resiliency.

NOTE: some of this is aspirational at the time of this writing, for our L2
resiliency planning goal to be demonstrated in early 2022.

### Disordered bring-up

The broker state machine ensures that a starting broker pauses until its TBON
parent completes the rc1 script before starting its own rc1 script, so that
upstream services are online before downstream ones start.  As a result, it
is feasible to configure Flux to start automatically, then power on the entire
cluster at once and let Flux sort itself out.

If some nodes take longer to start up, or don't start at all, then those nodes
and their TBON children, if any, will remain offline until they do start.
The TBON has a fixed topology determined by configuration, and brokers do not
adapt to route around down nodes.  In addition, Flux must be completely stopped
to alter the topology configuration - it cannot be changed on the fly.

See the Flux Administrator's Guide for a discussion on draining nodes and
excluding nodes from scheduling via configuration.  Scheduling is somewhat
orthogonal to this topic.

### Subtree shut down

Flux is stopped administratively with `systemctl stop flux`.  This is may
be run on any broker, and will affect the broker's TBON subtree.  If run on
the rank 0 broker, the entire Flux instance is shut down.

Upon receiving SIGTERM from systemd, the broker informs its TBON children that
it is shutting down, and waits for them to complete rc3 in leaves-to-root
order, thus ensuring that the instance captures any state held on those
brokers, such as KVS content.

A broker that is cleanly shut down withdraws itself as a peer from its TBON
parent.  Future RPCs to the down broker automatically receive an EHOSTUREACH
error response.

### Node crash

If a node crashes without shutting down its flux broker, state held by that
broker and its TBON subtree is lost if it was not saved to its TBON parent.

The TBON parent of the lost node detects that its child has stopped sending
messages.  The parent marks the child lost and future RPCs directed to
(or through) the crashed node receive an EHOSTUNREACH error response.  In
addition, RPCs are tracked at the broker overlay level, and any requests that
were directed to (or through) the lost node that are unterminated, as
defined by RFC 6, receive an EHOSTUNREACH error response.

The TBON children of the lost node similarly detect the lack of parent
messages.  The child marks the parent offline and future RPCs, as well as
existing unterminated RPCs to that node receive an EHOSTUNREACH error response.
These nodes then proceed as though they were shut down, however since they are
cut off from upstream, any RPCs executed in rc3 to save state will fail
immediately.  Effectively a _subtree panic_ results and data may be lost.

### Node returning to Service

When a lost node comes back up, or when an administratively shut down node
is restarted with `systemctl start flux`, the freshly started broker
attempts to join the instance via its TBON parent, just as if it were joining
for the first time, and carrying no state from the previous incarnation.

The broker peer is identified for response routing purposes by its UUID, which
changes upon restart.  In-flight responses directed to (or through) the
old UUID are dropped.  This is desirable behavior because matchtags from the
previous broker incarnation(s) might become confused with matchtags from the
current one, and the old responses are answering requests that the current
broker didn't send.

Systemd is configured to aggressively restart brokers that stop on their own,
so TBON children of the returning broker should also be attempting to join the
instance and may do so once the returning broker has completed the rc1 script.

### Network outage

Network outages that persist long enough are promoted to hard failures.

Case 1:  A TBON parent marks its child lost due to a temporary network
interruption, and the child has not yet marked the parent lost when
communication is restored.  In this case, the parent sees messages from the
lost UUID, and sends a kiss of death message to the child, causing a subtree
panic at the child.  The subtree restarts and rejoins the instance.

Case 2:  The TBON child marks its parent lost due to a temporary network
interruption, and the parent has not yet marked the child lost when
communication is restored.  Assuming the child subtree has restarted,
the child introduces itself to the parent with a new UUID.  Before allowing
the child to join, it marks the old UUID lost and fails unterminated RPCs
as described above.  It then accepts the child's introduction and allows
the subtree to join.

### Diagnostics

#### Subtree Health Check

Each broker maintains a subtree health state that depends on the health
state reported by its TBON children.  The states are as follows:

**Name** | **Description**
---      | ---
Full     | online and no children partial/offline/degraded/lost
Partial  | online, some children partial/offline; no children degraded/lost
Degraded | online, some children degraded/lost
Lost     | gone missing (according to parent)
Offline  | not yet seen, or cleanly shut down (according to parent)

A user may quickly assess the overall health of the overlay network by
requesting the subtree health at rank 0.  If the state is reported as
_partial_ or _degraded_, then the TBON may be probed further for details
using the following algorithm:

1. Request state of TBON children from target rank
2. List TBON children in _lost_ or _offline_ state
3. For each child in _partial_ or _degraded_ state, apply this algorithm
on child rank

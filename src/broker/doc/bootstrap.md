## Broker Bootstrap Sequence

Each broker executes a bootstrap sequence to
1. get the size of the Flux instance
2. get its rank within the Flux instance
3. get its level within the hierarchy of Flux instances
4. get the mapping of broker ranks to nodes
5. compute the broker ranks of its peers (TBON parent and children)
6. get URI(s) of peers
7. get public key(s) of peers to initialize secure communication

An instance bootstraps using one of two mechanisms:  PMI or Config File.

### PMI

When Flux is launched by Flux, by another resource manager, or as a
standalone test instance, PMI is used for bootstrap.

The broker PMI client uses a subset of PMI capabilities, abstracted for
different server implementations in `pmiutil.c`.  The bootstrap sequence
itself is implemented in `boot_pmi.c`.  The sequence roughly follows the
steps outlined above.

Steps 1-4 involve accessing parameters directly provided by the PMI server.

In step 5, the fixed tree topology (rank, size, k) is used to compute the
ranks of the TBON parent and TBON children, if any.

If a broker has TBON children, it binds to a 0MQ URI that the children will
connect to.  If step 4 indicates that all brokers mapped to a single node,
the socket is bound to a local IPC path in the broker rundir.  If brokers are
mapped to multiple nodes, the socket is bound to a TCP address on the interface
hosting the default route and a randomly assigned port.  These URIs cannot
be predicted by peers, so they must be exchanged via PMI.

In addition, each broker generates a unique public, private CURVE keypair.
The public keys must be shared with peers to enable secure communication.

In step 6-7, each broker rank stores a "business card" containing its 0MQ URI
and public key to the PMI KVS under its rank.  A PMI barrier is executed.
Finally, the business cards for any peers are loaded from the PMI KVS.
The bootstrap process is complete and overlay initialization may commence.

Debugging: set `FLUX_PMI_DEBUG=1` in the broker's environment for a trace of
the broker's client PMI calls on stderr.

#### Flux booting Flux as a job

When Flux launches Flux in the job environment, Flux is providing both the
PMI server via the Flux shell `pmi` plugin, and the PMI client in the broker.

The shell `pmi` plugin offers the PMI-1 wire protocol to the broker, by
passing an open file descriptor to each broker via the `PMI_FD` environment
variable.  The broker client uses the PMI-1 wire protocol client code in
`src/common/libpmi/simple_client.c` to execute the bootstrap sequence.

Debugging: set the shell option `verbose=2` for a server side trace on stderr
from the shell `pmi` plugin.

#### Booting Flux as a standalone test instance

An instance of size N may be launched on a single node using
`flux-start --test-size=N`.  In this case, the PMI server is embedded in
the start command, and the PMI-1 wire protocol is used as described above.

Debugging: use the `flux start --verbose=2` option for a server side
trace on stderr from the start command.

#### Booting Flux as a job in a foreign resource manager

The code in `boot_pmi.c` (using libpmi/upmi.c) attempts to adapt the broker's
PMI client to different situations if the PMI-1 wire protocol is not available.
It tries the following in order (unless configured otherwise):
1. simple PMI wire protocol
2. find a PMI-1 library
3. assume singleton (rank = 0, size = 1)

### Config File

When Flux is launched by systemd, the brokers go through a similar bootstrap
process as under PMI, except that information is read from a set of identical
TOML configuration files replicated across the cluster.  The TOML configuration
contains a host array.

In step 1-2, the broker scans the host array for an entry with a matching
hostname.  The array index of the matching entry is the broker's rank,
and also contains its URI.  The array size is the instance size.

Steps 3-4 are satisfied by assuming that in this mode, there is one broker
per node, and the instance is at the top (level 0) of the instance hierarchy.

In step 5, as above, the fixed tree topology (rank, size, k) is used to
compute the ranks of the TBON parent and TBON children, if any.

Instead of generating a unique CURVE keypair per broker, an instance
bootstrapped in this way shares a single CURVE keypair replicated across
the cluster.

So steps 6-7 are satisfied by simply accessing the CURVE key certificate
on disk, and looking up the peer rank indices in the hosts array.
The bootstrap process is complete and overlay initialization may commence.

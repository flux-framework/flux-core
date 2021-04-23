## Broker Bootstrap Sequence

Each broker must determine its place in the Flux instance:  rank,
size, and URI of TBON parent.  It may determine this by reading
a static set of config files, or using the Process Management Interface (PMI).

### PMI

When Flux is launched by Flux, by another resource manager, or by
`flux start [--testsize=N] ...`, PMI provides the broker rank and
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

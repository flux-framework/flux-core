_NOTE: This project is currently EXPERIMENTAL (as indicated by its
0.x.x version) and is being actively developed.  We offer no guarantee
of API stablility from release to release at this time._

### flux-core

flux-core implements the communication layer, lowest level
services and interfaces for the Flux resource manager framework.

#### Building flux-core

Flux requires the following packages to build:
```
zeromq4-devel >= 4.0.4   # built --with-libsodium
czmq-devel >= 2.2.0
munge-devel
json-c-devel
```
Spec files for building zeromq4 and czmq packages on a RHEL 6 based
system are provided for your convenience in foreign/rpm.

Make sure you have `mpicc` in your environment to build the MPI based
test programs.  If building from a release tarball you can skip
the first step.

```
./autogen.sh
./configure
make
make check
```
#### Bootstrapping a Flux comms session

The following starts a single-rank Flux comms session, and spawns
a shell.  Inside the shell the environment is set up so that flux
commands can find the message broker socket.
When the shell exits, the session exits.
```
cd src/cmd
./flux start-single
```

Alternatively, a multi-rank Flux comms session can be started under
screen (e.g. for a session of size 8):
```
cd src/cmd
./flux start-screen -N8
```
Rank 0 will spawn a shell but you have to connect to it by attaching
to window 0 of the screen session:
```
./flux start-screen --attach 0
```
or detach with
```
ctrl-a d
```
To terminate the screen session, run
```
./flux start-screen --shutdown
```

Finally, a multi-rank Flux comms session can be started under SLURM
(e.g. for a session of size 64):
```
./flux start-srun -N 64
```
The srun --pty option is used to connect to the rank 0 shell.
When you exit this shell, the session terminates.

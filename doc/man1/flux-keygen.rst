.. flux-help-include: true

==============
flux-keygen(1)
==============


SYNOPSIS
========

**flux** **keygen** [*--force*] [*--plain*]


DESCRIPTION
===========

flux-keygen(1) generates long-term keys for Flux security.
Keys are written to files in *$HOME/.flux*.

Flux comms sessions implement cryptographic privacy and data integrity
when data is sent over a network. Point to point ZeroMQ TCP connections
are protected with the CURVE security mechanism built into ZeroMQ
version 4, based on curve25519 and a CurveCP-like protocol.

It is possible to start a Flux comms session with security
disabled or using the toy PLAIN ZeroMQ security mechanism.
This is intended for testing performance overhead of security only.
By default, **flux-keygen** generates both CURVE and PLAIN keys.
PLAIN keys are merely RFC 4122 uuids stored in the clear that are
used as passwords.

All instances of Flux message brokers launched in a comms session
need to access your keys, therefore we assume that your *$HOME/.flux*
directory is globally accessible. Of course if keys are being transferred
in the clear across public networks to make this happen, you have
the same sort of problem as you might have with ssh private keys stored
in your home directory.

The Flux security design is not yet complete, and these measures
are probably not the final ones.


OPTIONS
=======

**-f, --force**
   Generate new keys, even if they already exist.

**-p, --plain**
   Generate PLAIN key.


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

ZAP: http://rfc.zeromq.org/spec:27

CurveZMQ: http://curvezmq.org/page:read-the-docs

ZMTP/3.0: http://rfc.zeromq.org/spec:23

Using ZeroMQ Security:
http://hintjens.com/blog:48 and
http://hintjens.com/blog:49

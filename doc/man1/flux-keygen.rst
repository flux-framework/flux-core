==============
flux-keygen(1)
==============


SYNOPSIS
========

**flux** **keygen** [*--name=NAME*] [*--meta=KEY=VAL...*] *PATH*


DESCRIPTION
===========

.. program:: flux keygen

:program:`flux keygen` generates a long-term CURVE certificate used to secure
the overlay network of a Flux system instance.

The Flux overlay network implements cryptographic privacy and data integrity
when data is sent over a network.  Point to point ZeroMQ TCP connections
are protected with the CURVE security mechanism built into ZeroMQ
version 4, based on curve25519 and a CurveCP-like protocol.

All brokers participating in the system instance must use the same
certificate.  The certificate is part of the bootstrap configuration.

Flux instances that bootstrap with PMI do not require a configured certificate.
In that case, each broker self-generates a unique certificate and the
public keys are exchanged with PMI.


OPTIONS
=======

:program:`flux keygen` accepts the following options:

.. option:: -n, --name=NAME

   Set the certificate metadata ``name`` field.  The value is logged when
   :man1:`flux-broker` authenticates a peer that presents this certificate.
   A cluster name might be appropriate here.  Default: the local hostname.

.. option:: --meta=KEY=VAL

   Set arbitrary certificate metadata.  Multiple key-value pairs may be
   specified, separated by commas.  This option may be specified multiple
   times.


RESOURCES
=========

.. include:: common/resources.rst

ZAP: http://rfc.zeromq.org/spec:27

CurveZMQ: http://curvezmq.org/page:read-the-docs

ZMTP/3.0: http://rfc.zeromq.org/spec:23

Using ZeroMQ Security: http://hintjens.com/blog:48, http://hintjens.com/blog:49

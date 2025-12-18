********************
Pre-flight Checklist
********************

Here are some things to check before going live with a new Flux system
instance.

Do I have all the right packages installed?
===========================================

Flux packages should be installed on all nodes.

.. list-table::
   :header-rows: 1

   * - Package name
   * - flux-core
   * - flux-security
   * - flux-sched
   * - flux-pam (optional)
   * - flux-accounting (optional)

Does /var/lib/flux have plenty of space on the leader node?
===========================================================

Flux stores its databases and KVS dumps in the ``statedir`` on the
leader (management) node.  Storage consumption depends on usage, the
size of the cluster, and other factors but be generous as running out
of space on the leader node is catastrophic to Flux.

The ``statedir`` is created automatically by systemd if it does not
exist when Flux is started.  If you are creating it, it should be owned
by the ``flux`` user and private to that user.

Is Munge working?
=================

Munge daemons must be running on every node, clocks must be synchronized,
and all nodes should be using the same pre-shared munge key.

.. code-block:: console

  $ pdsh -a systemctl is-active munge | dshbak -c
  ----------------
  test[0-7]
  ----------------
  active

  $ pdsh -a "timedatectl | grep synchronized:" | dshbak -c
  ----------------
  test[0-7]
  ----------------
  System clock synchronized: yes

  # spot check
  $ echo xyz | ssh test1 munge | ssh test2 unmunge
  STATUS:          Success (0)
  ENCODE_HOST:     test1 (192.168.88.246)
  ENCODE_TIME:     2024-04-18 09:41:21 -0700 (1713458481)
  DECODE_TIME:     2024-04-18 09:41:21 -0700 (1713458481)
  TTL:             300
  CIPHER:          aes128 (4)
  MAC:             sha256 (5)
  ZIP:             none (0)
  UID:             testuser (100)
  GID:             testuser (100)
  LENGTH:          4

Are users set up properly?
==========================

Flux requires that the ``flux`` user and all other users that will be
using Flux have the a consistent UID assignment across the cluster.

.. code-block:: console

  $ pdsh -a id flux | dshbak -c
  ----------------
  test[0-7]
  ----------------
  uid=500(flux) gid=500(flux) groups=500(flux)

Is the Flux network certificate synced?
=======================================

The network certificate should be identical on all nodes and should
only be readable by the ``flux`` user:

.. code-block:: console

  $ sudo pdsh -a md5sum /etc/flux/system/curve.cert | dshbak -c
  ----------------
  test[0-7]
  ----------------
  1b3c226159b9041d357a924841845cec  /etc/flux/system/curve.cert

  $ pdsh -a stat -c '"%U %A"' /etc/flux/system/curve.cert | dshbak -c
  ----------------
  test[0-7]
  ----------------
  flux -r--------

See :ref:`config_cert`.

Is the Flux configuration synced?
=================================

The Flux configurations for system, security, and imp should
be identical on all nodes, owned by root, and publicly readable:


.. code-block:: console

  $ pdsh -a "flux config get --config-path=system | md5sum" | dshbak -c
  ----------------
  test[1-7]
  ----------------
  432378ee4f210a879162e1ac66465c0e  -
  $ pdsh -a "flux config get --config-path=security | md5sum" |dshbak -c
  ----------------
  test[1-7]
  ----------------
  1c53f68eea714a1b0641f201130e0d29  -
  $ pdsh -a "flux config get --config-path=imp | md5sum" |dshbak -c
  ----------------
  test[0-7]
  ----------------
  e69c9d49356f4f1ecb76befdac727ef4  -

  $ pdsh -a stat -c '"%U %A"' /etc/flux/system/conf.d /etc/flux/security/conf.d /etc/flux/imp/conf.d |dshbak -c
  ----------------
  test[0-7]
  ----------------
  root drwxr-xr-x
  root drwxr-xr-x
  root drwxr-xr-x

Will the network be able to wire up?
====================================

Check your firewall rules and DNS/hosts configuration to ensure that each
broker will be able to look up and connect to its configured parent in the
tree based overlay network using TCP.

Will the network stay up?
=========================

Although TCP is a reliable transport, the network used by the Flux overlay
should be stable, otherwise:

- Nodes can be temporarily marked offline for scheduling if the Flux broker
  remains connected but cannot get messages through promptly.  This may be
  tuned with ``tbon.torpid_max``.

- Nodes can be disconnected (and running jobs lost) when TCP acknowledgements
  cannot get through in time.  For example, this may happen during a network
  partition.  This may be tuned with ``tbon.tcp_user_timeout``.

If the network is expected to be unstable (e.g. while the bugs are worked
out of new hardware), then the above values may need to be temporarily
increased to avoid nuisance failures.  See :man5:`flux-config-tbon`.

Is the Flux resource configuration correct?
===========================================

Ensure all nodes have the same resource configuration and that the summary
looks sane:

.. code-block:: console

  $ pdsh -a "flux R parse-config /etc/flux/system/conf.d | flux R decode --short" | dshbak -c
  ----------------
  test[0-7]
  ----------------
  rank[0-7]/core[0-3]

  $ pdsh -a "flux R parse-config /etc/flux/system/conf.d | flux R decode --nodelist" | dshbak -c
  ----------------
  test[0-7]
  ----------------
  test[0-7]

Does the leader broker start?
=============================

Try to start the leader (rank 0) broker on the management node.

.. code-block:: console

    $ sudo systemctl start flux
    $ flux uptime
     07:42:52 run 3.8s,  owner flux,  depth 0,  size 8,  7 offline
    $ systemctl status flux
      ● flux.service - Flux message broker
       Loaded: loaded (/lib/systemd/system/flux.service; enabled; vendor preset: enabled)
       Active: active (running) since Tue 2024-04-23 07:36:44 PDT; 37s ago
      Process: 287736 ExecStartPre=/usr/bin/loginctl enable-linger flux (code=exited, status=0/SUCCESS)
      Process: 287737 ExecStartPre=bash -c systemctl start user@$(id -u flux).service (code=exited, status=0/SUCCESS)
     Main PID: 287739 (flux-broker-0)
       Status: "Running as leader of 8 node Flux instance"
        Tasks: 22 (limit: 8755)
       Memory: 26.6M
          CPU: 3.506s
       CGroup: /system.slice/flux.service
               └─287739 broker --config-path=/etc/flux/system/conf.d -Scron.directory=/etc/flux/system/cron.d -Srundir=/run/flux -Sstatedir=/var/lib/flux -Slocal-uri=local:///run/flux/local -Slog-stderr-level=6 -Slog-stderr-mode=local -Sbroker.rc2_none -Sbroker.quorum=1 -Sbroker.quorum-warn=none -Sbroker.exit-norestart=42 -Sbroker.sd-notify=1 -Scontent.dump=auto -Scontent.restore=auto

  Apr 23 07:36:46 test0 flux[287739]: sched-fluxion-resource.info[0]: version 0.33.1-40-g24255b38
  Apr 23 07:36:46 test0 flux[287739]: sched-fluxion-qmanager.info[0]: version 0.33.1-40-g24255b38
  Apr 23 07:36:46 test0 flux[287739]: broker.info[0]: rc1.0: running /etc/flux/rc1.d/02-cron
  Apr 23 07:36:47 test0 flux[287739]: broker.info[0]: rc1.0: /etc/flux/rc1 Exited (rc=0) 2.6s
  Apr 23 07:36:47 test0 flux[287739]: broker.info[0]: rc1-success: init->quorum 2.65475s
  Apr 23 07:36:47 test0 flux[287739]: broker.info[0]: online: test0 (ranks 0)
  Apr 23 07:36:47 test0 flux[287739]: broker.info[0]: quorum-full: quorum->run 0.102056s

Do other nodes join?
====================

Bring up a follower node that is configured with the leader as its parent
in the tree based overlay network:

.. code-block:: console

  $ ssh test1
  $ sudo systemctl start flux
  $ flux uptime
   07:47:58 run 4.3m,  owner flux,  depth 0,  size 8,  6 offline
  $ flux overlay status
  0 test0: partial
  ├─ 1 test1: partial
  │  ├─ 3 test3: offline
  │  ├─ 4 test4: offline
  │  └─ 5 test5: offline
  └─ 2 test2: offline
     ├─ 6 test6: offline
     └─ 7 test7: offline
  $ flux resource status
       STATE UP NNODES NODELIST
     avail  ✔      2 test[0-1]
    avail*  ✗      6 test[2-7]

If all goes well, bring up the remaining nodes:

.. code-block:: console

  $ sudo pdsh -a systemctl start flux
  $ flux overlay status
  0 test0: full
  ├─ 1 test1: full
  │  ├─ 3 test3: full
  │  ├─ 4 test4: full
  │  └─ 5 test5: full
  └─ 2 test2: full
     ├─ 6 test6: full
     └─ 7 test7: full
  $ flux resource status
     STATE UP NNODES NODELIST
     avail  ✔      8 test[0-7]

Are my queues started?
======================

If named queues are configured, they will be initially stopped, meaning
jobs can be submitted but won't run.  Enable all queues with

.. code-block:: console

  $ sudo flux queue start --all
  debug: Scheduling is started
  batch: Scheduling is started

Can I run a job as a regular user?
==================================

Flux should be able to run jobs as an unprivileged user:

.. code-block:: console

  $ id
  uid=1000(pi) gid=1000(pi) groups=1000(pi),27(sudo),114(netdev)
  $ flux run -N8 id
  uid=1000(pi) gid=1000(pi) groups=1000(pi),27(sudo),117(netdev)
  uid=1000(pi) gid=1000(pi) groups=1000(pi),27(sudo),117(netdev)
  uid=1000(pi) gid=1000(pi) groups=1000(pi),27(sudo),117(netdev)
  uid=1000(pi) gid=1000(pi) groups=1000(pi),27(sudo),117(netdev)
  uid=1000(pi) gid=1000(pi) groups=1000(pi),27(sudo),117(netdev)
  uid=1000(pi) gid=1000(pi) groups=1000(pi),27(sudo),117(netdev)
  uid=1000(pi) gid=1000(pi) groups=1000(pi),27(sudo),117(netdev)
  uid=1000(pi) gid=1000(pi) groups=1000(pi),27(sudo),117(netdev)

Prometheus Plugin
=================

Prometheus is a metrics server and time series database that makes it easy to monitor applications. In the context of Flux, our
first use case to be interested in exporting metrics is to allow `autoscaling <https://github.com/flux-framework/flux-core/issues/5214>`_,
however the plugin could be used in any context where metrics are useful, and the metrics can be extended as needed.

To allow this to happen, there are two things you need to do:

1. Build the `prometheus-cpp library <https://github.com/jupp0r/prometheus-cpp>`_ that provides the data interface
2. Add an appropriate flag to the configure command to ensure the plugin is built
3. Enable the plugin by adding a module load command to your rc1 file.

This small guide will provide you with instructions for doing these tasks!

Dependencies
------------

There is one dependency!

.. code-block:: console

    sudo apt-get install -y texinfo
    git clone https://github.com/scottjg/libmicrohttpd /tmp/micro
    cd /tmp/micro
    ./bootstrap
    ./configure --prefix=/usr
    make 
    sudo make install

Building the Library
--------------------

Note that if you are using the Flux DevContainer, you should have all the dependencies that you need,
primarily cmake.

.. code-block:: console

   $ git clone https://github.com/digitalocean/prometheus-client-c /tmp/prometheus-client-c/
   $ cd /tmp/prometheus-client-c/prom
   $ mkdir _build
   $ cd _build
   $ cmake ../
   $ make
   $ sudo make install

   $ cd /tmp/prometheus-client-c/promhttp
   $ mkdir _build
   $ cd _build
   $ cmake ../
   $ make
   $ sudo make install

And that's it! You should see stuffs installed to /usr/local, which should be fine for our use cases.


Compiling
---------

Once you've built the library, when you configure Flux you can add a flag that indicates "please build the prometheus plugin!"
If you install to a non-standard location, you likely want to add your library to the `LD_LIBRARY_PATH`. I'm sure there are more elegant
ways to do it.

.. code-block:: console

    $ export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/path/to/my/special/lib

And then configure!

.. code-block:: console

    $ ./configure --enable-prometheus

If your library is found, it should finish without a hitch. You can next proceed to build Flux.
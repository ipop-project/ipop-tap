======================================
IPOP Tap
======================================

This is the tap module for IPOP that performs all packet-level operations,
translating packets, setting up, and reading and writing from and to the TAP
device.

How to compile
==============

On Debian GNU/Linux (and derivatives)
-------------------------------------

1. Check to make sure you have OpenSSL installed (as root)::
      
      sudo aptitude install libssl-dev openssl libjansson-dev gyp
   
2. Build the software (doesn't need root)::
      
      gyp build/ipop_tap.gyp --depth .
      make

On Android (Mobile, ARM)
------------------------

1. Navigate to ``svpn/android/jni``::
       
       cd android/jni

2. Decompress the pre-built libssl and libcrypto binaries::
       
       gunzip -c libcrypto.a.gz > libcrypto.a
       gnuzip -c libssl.a.gz > libssl.a

3. Run the included helper setup script to download the openssl headers::
       
       ./setup.sh

4. Use ndk-build (assuming it in your path) to build the application::
       
       ndk-build


How to run
==========

On Debian GNU/Linux (and derivatives)
-------------------------------------

.. note::

1. Make a new ``config.json`` file based on ``config.json.example``.
2. Starting in the base repository directory::
       
       sudo ./bin/ipop-tap

.. note::
   You can see information on command-line arguments that the program supports
   by using a ``-h`` or ``--help`` flag.

.. warning::
   At the moment this is simply a conceptual prototype, and there is no IPv4
   encryption in place. We plan to use TLS from OpenSSL, but that's for a future
   date.

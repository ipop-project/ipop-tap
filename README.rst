======================================
SocialVPN Core - A P2P VPN for Friends
======================================

This is the core module for SocialVPN that performs all packet-level operations,
translating packets, setting up, and reading and writing from and to the TAP
device.

How to compile
==============

On Debian GNU/Linux (and derivatives)
-------------------------------------

1. Check to make sure you have OpenSSL installed (as root)::
      
      sudo aptitude install libssl-dev openssl
   
   If you plan on using IPSec, you'll also want to install Racoon and
   IPSec-Tools (as root)::
      
      sudo aptitude install racoon ipsec-tools

2. Build the software (doesn't need root)::
      
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
   You will need to run these commands as root. SocialVPN will drop privileges
   to ``nobody`` after initialization.

1. Make a new ``config.json`` file based on ``config.json.example``.
2. Starting in the base repository directory::
       
       sudo ./bin/svpn

.. note::
   You can see information on command-line arguments that the program supports
   by using a ``-h`` or ``--help`` flag.

IPSec for IPv6
==============

As of writing, IPSec does not work with Android, but it does work on the
desktop:

1. Generate an RSA public/private keypair using the ``utils/make_key_pair.sh``
   utility::
       
       mkdir keys
       ./utils/make_key_pair.sh keys/my_priv_key.priv keys/my_pub_key.pub 2048
   
   The last argument is the number of bits to use for the keysize. This number
   should be **at least** 1024 bits\ [#]_, and is often a power of two.

   .. note::
      Racoon requires a special format for its public/private keys, so it is
      highly recommended that you stick with the provided script for key
      generation.

.. [#] Just recently (as of writing), in the wake of the high profile Flame
   malware targeting Windows, Microsoft has begun revoking all their internal
   certificates less than 1024 bits, because keys of 512 bits or less have been
   widely proven crackable. For new keys, most people suggest using at least
   2048 bits. The RSA suggests that 2048 bit keys should safe until around 2030,
   while 1024 bit keys are on the verge of being crackable. Longer keys mean
   more security, but a higher computational cost.

2. Share the public key you just generated with anyone you'd like. It is a
   cryptographically secure way of verifying your identity. Keep your private
   key in a safe place. If someone else gets that, they can forge your identity.
3. Ensure IPSec is enabled for your client and for the peers you care about.
   Look at the example configuration file for information on how this is done.
4. Make sure your friends also have IPSec enabled.
5. Run ``svpn``!

Troubleshooting (Debian)
------------------------

With Debian, you can restart the ``racoon`` and ``setkey`` daemons with the
following::
    
    sudo /etc/init.d/racoon restart
    sudo /etc/init.d/setkey restart

Racoon writes its logs to the syslog. On Debian, this is ``/var/log/syslog``
(can only be read by root)::
    
    sudo tail /var/log/syslog

.. warning::
   At the moment this is simply a conceptual prototype, and there is no IPv4
   encryption in place. We plan to use TLS from OpenSSL, but that's for a future
   date.

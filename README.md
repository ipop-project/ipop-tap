IPOP TAP
========

This is the TAP module for IPOP that performs all packet-level operations,
translating packets, setting up, and reading and writing from and to the TAP
device.

It also serves as a command line tool that can shuttle traffic to and from a TAP
device through a UDP socket, making it an excellent starting point for
implementing your own VPN.

How to compile
--------------

`ipop-tap` isn't generally useful on its own. Typically, [you'll want to build
it with `ipop-tincan`](https://github.com/ipop-project/documentation/wiki).

These instructions exist for those who wish to use `ipop-tap` on its own as a
command-line tool, either for testing and development, or for use with some
other application.

> **warning**
>
> This doesn't implement any form of secure authentication or encryption. If you
> want that, you'll have roll your own, or use it with something else that
> provides it, such as ipop-tincan.

### On Debian GNU/Linux (and derivatives)

1.  Check to make sure you have our build dependencies:

        sudo aptitude install build-essential

2.  Build the software (doesn't need root):

        make

### On Android (Mobile, ARM)

1.  Navigate to `svpn/android/jni`:

        cd android/jni

2.  Use ndk-build (assuming it in your path) to build the application:

        ndk-build

How to run
----------

### On Debian GNU/Linux (and derivatives)

1.  Make a new `config.json` file based on `config.json.example`.
2.  Starting in the base repository directory:

        sudo ./bin/ipop-tap

> **note**
>
> You can see information on command-line arguments that the program supports by
> using a `-h` or `--help` flag.

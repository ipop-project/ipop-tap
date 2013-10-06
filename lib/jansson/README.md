Jansson 2.3.1
=============

Jansson is a C library for encoding, decoding and manipulating JSON data. It
features:

-   Simple and intuitive API and data model
-   Comprehensive documentation *(not included in repackaging)*
-   No dependencies on other libraries
-   Full Unicode support (UTF-8)
-   Extensive test suite *(not included in repackaging)*

Packaging
---------

This is a repackaged version of [janson][]. I just took the tar.gz file from the
website, extracted the `src` directory, and removed the Autoconf specific chunks
left in there, along with the `github_commits.c` example. This component is not
intended to be used separately, but rather in conjunction with svpn as a whole.
The main svpn Makefile should take care of this for you, statically linking it
in.

  [janson]: http://www.digip.org/jansson/

Licensing
---------

Janson is available under the MIT License. Seeing as we've made no modifications
to it (aside from deleting a couple files), the license stays.

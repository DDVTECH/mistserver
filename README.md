MistServer
==========

[![Build Status](https://drone.livepeer.fish/api/badges/DDVTECH/mistserver/status.svg?ref=refs/heads/catalyst)](https://drone.livepeer.fish/DDVTECH/mistserver)

MistServer is an open source, public domain, full-featured, next-generation streaming media toolkit for OTT (internet streaming), designed to be ideal for developers and system integrators.

For full documentation, tutorials, guides and assistance, please look on our website at: https://mistserver.org

Getting MistServer onto your system
===================================

We provide precompiled binaries for most common operating systems here: https://mistserver.org/download

Using the "Copy install cmd" button will give you a command you can paste into a terminal to set up MistServer running as root under your system's init daemon (systemd recommended, but not required).

You can also manually install, will instructions can be found in our manual: https://mistserver.org/guides/latest

Compile instructions
====================

We make use of cmake for compilation. The default configuration requires mbedtls and libsrtp2 to be installed on your system.

The version of mbedtls we require is a specific branch that supports DTLS and SRTP for WebRTC, which can be found here: https://github.com/livepeer/mbedtls/tree/dtls_srtp_support

All compilaton options can be discovered and set through `cmake-gui`; more complete compile instructions will follow soon.

Usage
=====

MistServer is booted by starting the `MistController` binary, which then scans the directory it is stored in for further `Mist*` binaries and runs them to discover what inputs/outputs/processes are available.

Running the controller in a terminal will walk you through a brief first-time setup, and then listen on port 4242 for API connections. Accessing port 4242 from a web browser will bring up a web interface capable of easily running most API commands for human-friendly configuration.

Full usage instructions and API specifications can be found in the manual: https://mistserver.org/guides/latest

Contributing
============

If you're interested in contributing to MistServer development, please reach out to us through info@mistserver.org. Full contribution guidelines will be made available soon.

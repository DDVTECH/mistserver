MistServer
==========

MistServer is an open source, public domain, full-featured, next-generation streaming media toolkit for OTT (internet streaming), designed to be ideal for developers and system integrators.

For full documentation, see: https://docs.mistserver.org

For support and assistance, please look on our website at: https://mistserver.org

Getting MistServer onto your system
===================================

We provide pre-compiled binaries for most common operating systems here: https://mistserver.org/download

Using the "Copy install cmd" button will give you a command you can paste into a terminal to set up MistServer running as root under your system's init daemon (systemd recommended, but not required).

You can also manually install, full instructions can be found in our manual: https://docs.mistserver.org/category/installation

Compile instructions
====================

The only supported build system for compiling MistServer is Meson, since CMake support was discontinued in MistServer 3.4.

The project makes full use of Meson's support for "wraps" and all dependencies can be automatically fulfilled through this system. If a system-wide library is available (and compatible), that one will be preferred.

The following command will create a subdirectory named `build` and set it up for compiling MistServer (assuming meson is installed on your system):

```
meson setup build
```

The default options should suffice in most cases, but a full list of possible compile options can be found by running `meson configure`.

Then, to actually build:

```
cd build
ninja
```

That should compile MistServer to your build directory, and it can then be ran by running:

```
./MistController
```

See the "Usage" chapter below for more details on actually running MistServer.
MistServer can be in any directory, as long as all its binaries (that you want/need) are in one directory together.
You can (optionally) install system-wide (usually requires you to be root user or using `sudo`) by running:

```
ninja install
```

Usage
=====

MistServer is booted by starting the `MistController` binary, which then scans the directory it is stored in for further `Mist*` binaries and runs them to discover what inputs/outputs/processes are available. (Yes, this means you can delete any binary you don't want/need and it will just do what you expect/want.)

Running the controller in a terminal will walk you through a brief first-time setup, and then listen on port 4242 for API connections.
Accessing port 4242 from a web browser will bring up a web interface capable of easily running most API commands for human-friendly configuration.
If there is no interactive terminal when MistServer is first started, the first-time setup can be completed using the web interface instead.

Full usage instructions and API specifications can be found in the online manual: https://docs.mistserver.org/

Contributing
============

If you're interested in contributing to MistServer development, please reach out to us through info@mistserver.org. Full contribution guidelines will be made available soon.


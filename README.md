## Eteroj

### Open Sound Control inside LV2 plugin graphs

This LV2 plugin bundle allows you to handle OSC (Open Sound Control) bundles and
messages in modular hosts.

* Insert/dispatch OSC from/to UDP, TCP (IPv4/6) and serial sockets
* Pack/unpack MIDI into OSC
* Pack/unpack OSC into MIDI
* Pack/unpack LV2 atoms into OSC

Makes only sense to be used in hosts that allow
routing of LV2 atom messages between plugins, e.g.

* <http://drobilla.net/software/ingen>
* <https://open-music-kontrollers.ch/lv2/synthpod>

#### Build status

[![build status](https://gitlab.com/OpenMusicKontrollers/eteroj.lv2/badges/master/build.svg)](https://gitlab.com/OpenMusicKontrollers/eteroj.lv2/commits/master)

### Binaries

For GNU/Linux (64-bit, 32-bit, armv7), Windows (64-bit, 32-bit) and MacOS
(64/32-bit univeral).

To install the plugin bundle on your system, simply copy the __eteroj.lv2__
folder out of the platform folder of the downloaded package into your
[LV2 path](http://lv2plug.in/pages/filesystem-hierarchy-standard.html).

#### Stable release

* [eteroj.lv2-0.6.0.zip](https://dl.open-music-kontrollers.ch/eteroj.lv2/stable/eteroj.lv2-0.6.0.zip) ([sig](https://dl.open-music-kontrollers.ch/eteroj.lv2/stable/eteroj.lv2-0.6.0.zip.sig))

#### Unstable (nightly) release

* [eteroj.lv2-latest-unstable.zip](https://dl.open-music-kontrollers.ch/eteroj.lv2/unstable/eteroj.lv2-latest-unstable.zip) ([sig](https://dl.open-music-kontrollers.ch/eteroj.lv2/unstable/eteroj.lv2-latest-unstable.zip.sig))

### Sources

#### Stable release

* [eteroj.lv2-0.6.0.tar.xz](https://git.open-music-kontrollers.ch/lv2/eteroj.lv2/snapshot/eteroj.lv2-0.6.0.tar.xz)

#### Git repository

* <https://git.open-music-kontrollers.ch/lv2/eteroj.lv2>

### Packages

* [ArchLinux](https://www.archlinux.org/packages/community/x86_64/eteroj.lv2/)

### Bugs and feature requests

* [Gitlab](https://gitlab.com/OpenMusicKontrollers/eteroj.lv2)
* [Github](https://github.com/OpenMusicKontrollers/eteroj.lv2)

### Plugins

#### (De)Cloak

Embed OSC in MIDI Sysex messages. Use this to smuggle arbitrary OSC packets
via MIDI to a given destination. It does also the opposite of course, e.g
extract arbitrary OSC packets previously embedded in MIDI Sysex messages.

#### IO

A plugin able to inject/eject [OSC](http://opensoundcontrol.org)
packets into/from the plugin graph to/from network and serial lines. The
non-realtime network part of the plugin supports OSC via bidirectional UDP
and TCP on top of IPv4/IPv6 and OSC via serial lines which is handy for
interfacing to microcontrollers via U(S)ART and USB. TCP based connections
support both (deprecated) size-prefix and SLIP framing (default).

Internally to the plugin graph, OSC packets are routed as first-class
LV2 Atom objects, making the plugin compliant with any existing hosts.

Timestamped OSC bundles are injected into the plugin graph with sample
accuracy.

The supported Urls are as follows:

	// UDP IPv4 unicast server/client on port 2222
	osc.udp://:2222
	osc.udp://localhost:2222
	
	// UDP IPv6 unicast server/client on port 3333
	osc.udp://[]:3333
	osc.udp://[::1]:3333

	// UDP IPv4 broadcast server/client on port 3344
	osc.udp://:3344
	osc.udp://255.255.255.255:3344

	// TCP IPv4 server/client on port 4444 (SLIP encoded)
	osc.tcp://:4444
	osc.tcp://localhost:4444

	// TCP IPv6 server/client on port 5555 (SLIP encoded)
	osc.tcp://[]:5555
	osc.tcp://[::1]:5555

	// TCP IPv4 server/client on port 6666 (SLIP encoded)
	osc.slip.tcp://:6666
	osc.slip.tcp://localhost:6666
	
	// TCP IPv6 server/client on port 7777 (SLIP encoded)
	osc.slip.tcp://[]:7777
	osc.slip.tcp://[::1]:7777
	
	// TCP IPv4 server/client on port 8888 (int32 size-prefixed)
	osc.prefix.tcp://:8888
	osc.prefix.tcp://localhost:8888
	
	// TCP IPv6 server/client on port 9999 bound to loopback interface (int32 size-prefixed)
	osc.prefix.tcp://[%lo]:9999
	osc.prefix.tcp://[::1%lo]:9999

	// Serial server/client on /dev/ttyUSB0 (SLIP encoded)
	osc.serial:///dev/ttyUSB0


### Ninja

Embed Turtle RDF in OSC as string. Use this to smuggle arbitrary LV2 atom
messages via OSC to a given destination. It does also the opposite of course,
e.g. extract and deserialize Turtle RDF embedded in OSC messages to plain
LV2 atoms.

### (Un)Pack

Embed arbitrary 1-3 byte MIDI commands (but Sysex) in OSC messages. Use this to
send MIDI commands via OSC to a given destination. It does also the opposite
of course, e.g.  extract MIDI commands embedded in OSC messages to plain MIDI.

### Query

This plugin implements our [OSC Introspect](/osc/introspect/#)
specification. It thus exports any methods and parameters of a given OSC
device to transparently to LV2 properties.

#### License

Copyright (c) 2016-2019 Hanspeter Portner (dev@open-music-kontrollers.ch)

This is free software: you can redistribute it and/or modify
it under the terms of the Artistic License 2.0 as published by
The Perl Foundation.

This source is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
Artistic License 2.0 for more details.

You should have received a copy of the Artistic License 2.0
along the source as a COPYING file. If not, obtain it from
<http://www.perlfoundation.org/artistic_license_2_0>.

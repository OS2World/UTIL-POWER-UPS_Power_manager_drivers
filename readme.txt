UPS Power manager drivers
=========================

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

	Please see the file COPYING which should have been provided with this
	archive.

Introduction
============

The power manager relies on getting a response from a UPS device connected to
your computer by some means. The means used is entirely dependant on the
driver, and could be a serial port or a usb port for example.

The drivers initially provided use a threaded double buffered approach to the
required communication. This means that the speed at which the UPS is polled
can be totally independant of the UPS power manager - which polls at 1s
intervals.

There is room for improvement in these drivers. If you wish to improve upon
them, or create new drivers for other UPS devices, please have a look at the
http://random.networkupstools.org website, where you will find the required
communication protocols for a large number of UPS devices.

Files, and how it works
=======================

common.h	This file describes the common interface between drivers and the
			UPS power manager. It is the only file that is truly relevant to
			any driver you might choose to develop.

all other files are provided as is!

Essentially, this is what happens:-

UPS power manager loads the driver, and reads the driver functions

'UpsStatus' 	at ordinal 10
'UpsPortlist'	at ordinal 20

UpsPortlist is then called to fill in the port selections available for this
driver. Settings such as comms speed and so on are not really relevant as the
UPS device determines that!

Then the init function is called:-
UpsStatus(hab, cmd_init, "comx", &upsStatus);

This at least should return a string in 'upsStatus.infostr':-
UpsStatus(hab, cmd_info, "comx", &upsStatus);

...and then once a second:-
UpsStatus(hab, cmd_poll, 0L, &upsStatus);

At this point, the UPS should be providing data back to the application.
Please make sure that you fill in the valid_xxx flags in 'upsStatus.upsstatus'
so that the UPS power manager knows what it can display!

If there is a power fail, the UPS power manager waits for a given number of
seconds, and then the following happens:-

MIXED st = d_to_Mix(1.0);	//the UPS should SWITCH OFF in 1 minute
UpsStatus(hab, cmd_shutdown, (MPARAM)(st.val), NULL);
UpsStatus(hab, cmd_exit, NULL, NULL);

The UPS power manager then shuts down the computer!

Other messages are sent by user interaction like a button click for example.
See common.h for more messages.

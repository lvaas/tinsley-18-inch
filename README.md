# tinsley-18-inch
Software for various processors in the Tinsley 18-inch Cassegrain telescope owned by LVAAS.

The 18-inch Cassegrain telescope was built by Tinsley Laboratories and
installed at Kutztown University in 1968.  Upon retirement it was donated to
Lehigh Valley Amateur Astronomical Society, Inc. (https://lvaas.org/) and
placed in service in 2013. The original electrical system was replaced using
various embedded processor boards, and this repo contains the source code of
the operating software.

## System Description

The primary processor for the instrument is a Raspberry Pi Model B, which is
installed in the main electronics chassis and has an Ethernet connection to a
wi-fi router in the first floor office. As of this writing this network is
local-only, with no external connection available.

The Pi is fitted with an Alamode Arduino-compatible daughter board with which
it communicates using I2C.  The Alamode generates real-time signals to control
the motors in the telescope, including precision near-120Hz pulses that
determine the speed of the RA tracking motor.

The hand paddle contains a Teensy3.0 processor and emulates a USB keyboard. It
is connected to a USB port on the Pi, which receives keystroke events from it
via the /dev/input/event0 device. This is the primary method of controlling the
system.

There is a Teensy3.2 which replaced an analog board from the original 1968
design, which had failed. It receives the ~120Hz pulses from the Alamode and
generates driver signals for a 60-Hz AC inverter that drives the motor. It also
has some safety and test features that are described in the source code.

## Installation

The `main_processor/` subtree contains every change required after the
original raspbian install and update/upgrade, and installation of required
Python modules, to configure the processor as deployed. This includes
`/home/lvaas/python/telescope.py`, the primary operating program, which is
started from `/etc/rc.local` on boot. (See the initial commit message for
more details.)

The Teensy programs are installed on their respective hosts using the standard
Arduino development software, with the Teensy package from PJRC
(https://www.pjrc.com/teensy/td_download.html). The Alamode is designed to be
programmed from the Arduino IDE installed on the Raspberry Pi
(https://wyolum.com/projects/alamode/alamode-downloads/).

## Release History

* 2013
    - Code as developed when the telescope was refurbished by LVAAS.
* 2017
    - Add ra_driver Teensy3.2 processor as part of replacement inverter for
    tracking motor.
* 2022
    - Field patches to work around intermittent tracking rate selector
    switch, and correct a timing mismatch between main_processor and
    motor_controller in "guide East" mode.

## Meta

Rich Hogg – theotherplanb@gmail.com

Copyright 2013-2023 Lehigh Valley Amateur Astronomical Society, Inc.
All Rights Reserved.

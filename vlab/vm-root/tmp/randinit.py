#!/usr/bin/env python

import fcntl

fd = open('/dev/random', 'w')
fcntl.ioctl(fd.fileno(), 0x40045201, b'\x00\x01\x00\x00')

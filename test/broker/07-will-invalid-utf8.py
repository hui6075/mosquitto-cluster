#!/usr/bin/env python

# Test whether a will topic with invalid UTF-8 fails

import time
import inspect, os, sys
# From http://stackoverflow.com/questions/279237/python-import-a-module-from-a-folder
cmd_subfolder = os.path.realpath(os.path.abspath(os.path.join(os.path.split(inspect.getfile( inspect.currentframe() ))[0],"..")))
if cmd_subfolder not in sys.path:
    sys.path.insert(0, cmd_subfolder)

import struct
import mosq_test

rc = 1
mid = 53
keepalive = 60
connect_packet = mosq_test.gen_connect("will-invalid-utf8", keepalive=keepalive, will_topic="invalid/utf8")

b = list(struct.unpack("B"*len(connect_packet), connect_packet))
b[40] = 0 # Topic should never have a 0x0000
connect_packet = struct.pack("B"*len(b), *b)

port = mosq_test.get_port()
broker = mosq_test.start_broker(filename=os.path.basename(__file__), port=port)

try:
    sock = mosq_test.do_client_connect(connect_packet, "", timeout=30, port=port)
    rc = 0
    sock.close()
finally:
    broker.terminate()
    broker.wait()
    (stdo, stde) = broker.communicate()
    if rc:
        print(stde)

exit(rc)


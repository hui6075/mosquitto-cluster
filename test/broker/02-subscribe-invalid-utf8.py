#!/usr/bin/env python

# Test whether a SUBSCRIBE to a topic with an invalid UTF-8 topic fails

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
connect_packet = mosq_test.gen_connect("subscribe-invalid-utf8", keepalive=keepalive)
connack_packet = mosq_test.gen_connack(rc=0)

subscribe_packet = mosq_test.gen_subscribe(mid, "invalid/utf8", 0)
b = list(struct.unpack("B"*len(subscribe_packet), subscribe_packet))
b[13] = 0 # Topic should never have a 0x0000
subscribe_packet = struct.pack("B"*len(b), *b)

suback_packet = mosq_test.gen_suback(mid, 0)

port = mosq_test.get_port()
broker = mosq_test.start_broker(filename=os.path.basename(__file__), port=port)

try:
    time.sleep(0.5)

    sock = mosq_test.do_client_connect(connect_packet, connack_packet, port=port)
    sock.send(subscribe_packet)

    if mosq_test.expect_packet(sock, "suback", ""):
        rc = 0

    sock.close()
finally:
    broker.terminate()
    broker.wait()
    (stdo, stde) = broker.communicate()
    if rc:
        print(stde)

exit(rc)


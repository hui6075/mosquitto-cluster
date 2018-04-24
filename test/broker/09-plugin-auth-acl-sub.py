#!/usr/bin/env python

# Test topic subscription. All topic are allowed but not using wildcard in subscribe.

import inspect, os, sys
# From http://stackoverflow.com/questions/279237/python-import-a-module-from-a-folder
cmd_subfolder = os.path.realpath(os.path.abspath(os.path.join(os.path.split(inspect.getfile( inspect.currentframe() ))[0],"..")))
if cmd_subfolder not in sys.path:
    sys.path.insert(0, cmd_subfolder)

import mosq_test

def write_config(filename, port):
    with open(filename, 'w') as f:
        f.write("port %d\n" % (port))
        f.write("auth_plugin c/auth_plugin.so\n")
        f.write("allow_anonymous false\n")

port = mosq_test.get_port()
conf_file = os.path.basename(__file__).replace('.py', '.conf')
write_config(conf_file, port)

rc = 1
keepalive = 10
connect_packet = mosq_test.gen_connect("connect-uname-pwd-test", keepalive=keepalive, username="readonly")
connack_packet = mosq_test.gen_connack(rc=0)

mid = 53
subscribe_packet = mosq_test.gen_subscribe(mid, "qos0/test", 0)
suback_packet = mosq_test.gen_suback(mid, 0)

mid_fail = 54
subscribe_packet_fail = mosq_test.gen_subscribe(mid_fail, "#", 0)
suback_packet_fail = mosq_test.gen_suback(mid_fail, 0x80)

broker = mosq_test.start_broker(filename=os.path.basename(__file__), use_conf=True, port=port)

try:
    sock = mosq_test.do_client_connect(connect_packet, connack_packet, timeout=20, port=port)
    sock.send(subscribe_packet)

    if mosq_test.expect_packet(sock, "suback", suback_packet):
        sock.send(subscribe_packet_fail)
        if mosq_test.expect_packet(sock, "suback", suback_packet_fail):
            rc = 0

    sock.close()
finally:
    os.remove(conf_file)
    broker.terminate()
    broker.wait()
    (stdo, stde) = broker.communicate()
    if rc:
        print(stde)


exit(rc)


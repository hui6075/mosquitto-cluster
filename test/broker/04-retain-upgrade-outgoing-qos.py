#!/usr/bin/env python

# Test whether a retained PUBLISH to a topic with QoS 0 is sent with subscriber QoS
# when upgrade_outgoing_qos is true

import inspect, os, sys
# From http://stackoverflow.com/questions/279237/python-import-a-module-from-a-folder
cmd_subfolder = os.path.realpath(os.path.abspath(os.path.join(os.path.split(inspect.getfile( inspect.currentframe() ))[0],"..")))
if cmd_subfolder not in sys.path:
    sys.path.insert(0, cmd_subfolder)

import mosq_test

def write_config(filename, port):
    with open(filename, 'w') as f:
        f.write("port %d\n" % (port))
        f.write("upgrade_outgoing_qos true\n")

port = mosq_test.get_port()
conf_file = os.path.basename(__file__).replace('.py', '.conf')
write_config(conf_file, port)

rc = 1
keepalive = 60
mid = 16
connect_packet = mosq_test.gen_connect("retain-qos0-test", keepalive=keepalive)
connack_packet = mosq_test.gen_connack(rc=0)

publish_packet = mosq_test.gen_publish("retain/qos0/test", qos=0, payload="retained message", retain=True)
subscribe_packet = mosq_test.gen_subscribe(mid, "retain/qos0/test", 1)
suback_packet = mosq_test.gen_suback(mid, 1)

publish_packet2 = mosq_test.gen_publish("retain/qos0/test", mid=1, qos=1, payload="retained message", retain=True)

broker = mosq_test.start_broker(filename=os.path.basename(__file__), use_conf=True, port=port)

try:
    sock = mosq_test.do_client_connect(connect_packet, connack_packet, port=port)
    sock.send(publish_packet)

    #sock.close()
    #sock = mosq_test.do_client_connect(connect_packet, connack_packet, port=port)
    sock.send(subscribe_packet)

    if mosq_test.expect_packet(sock, "suback", suback_packet):
        if mosq_test.expect_packet(sock, "publish", publish_packet2):
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


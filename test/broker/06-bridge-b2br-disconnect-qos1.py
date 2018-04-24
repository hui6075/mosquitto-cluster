#!/usr/bin/env python

# Does a bridge resend a QoS=1 message correctly after a disconnect?

import socket

import inspect, os, sys
# From http://stackoverflow.com/questions/279237/python-import-a-module-from-a-folder
cmd_subfolder = os.path.realpath(os.path.abspath(os.path.join(os.path.split(inspect.getfile( inspect.currentframe() ))[0],"..")))
if cmd_subfolder not in sys.path:
    sys.path.insert(0, cmd_subfolder)

import mosq_test

def write_config(filename, port1, port2):
    with open(filename, 'w') as f:
        f.write("port %d\n" % (port2))
        f.write("\n")
        f.write("connection bridge_sample\n")
        f.write("address 127.0.0.1:%d\n" % (port1))
        f.write("topic bridge/# both 1\n")
        f.write("notifications false\n")
        f.write("restart_timeout 5\n")

(port1, port2) = mosq_test.get_port(2)
conf_file = os.path.basename(__file__).replace('.py', '.conf')
write_config(conf_file, port1, port2)

rc = 1
keepalive = 60
client_id = socket.gethostname()+".bridge_sample"
connect_packet = mosq_test.gen_connect(client_id, keepalive=keepalive, clean_session=False, proto_ver=128+4)
connack_packet = mosq_test.gen_connack(rc=0)

mid = 1
subscribe_packet = mosq_test.gen_subscribe(mid, "bridge/#", 1)
suback_packet = mosq_test.gen_suback(mid, 1)

mid = 2
subscribe2_packet = mosq_test.gen_subscribe(mid, "bridge/#", 1)
suback2_packet = mosq_test.gen_suback(mid, 1)

mid = 3
publish_packet = mosq_test.gen_publish("bridge/disconnect/test", qos=1, mid=mid, payload="disconnect-message")
publish_dup_packet = mosq_test.gen_publish("bridge/disconnect/test", qos=1, mid=mid, payload="disconnect-message", dup=True)
puback_packet = mosq_test.gen_puback(mid)

mid = 20
publish2_packet = mosq_test.gen_publish("bridge/disconnect/test", qos=1, mid=mid, payload="disconnect-message")
puback2_packet = mosq_test.gen_puback(mid)

ssock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
ssock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ssock.settimeout(40)
ssock.bind(('', port1))
ssock.listen(5)

broker = mosq_test.start_broker(filename=os.path.basename(__file__), port=port2, use_conf=True)

try:
    (bridge, address) = ssock.accept()
    bridge.settimeout(20)

    if mosq_test.expect_packet(bridge, "connect", connect_packet):
        bridge.send(connack_packet)

        if mosq_test.expect_packet(bridge, "subscribe", subscribe_packet):
            bridge.send(suback_packet)

            bridge.send(publish_packet)
            # Bridge doesn't have time to respond but should expect us to retry
            # and so remove PUBACK.
            bridge.close()

            (bridge, address) = ssock.accept()
            bridge.settimeout(20)

            if mosq_test.expect_packet(bridge, "connect", connect_packet):
                bridge.send(connack_packet)

                if mosq_test.expect_packet(bridge, "2nd subscribe", subscribe2_packet):
                    bridge.send(suback2_packet)

                    # Send a different publish message to make sure the response isn't to the old one.
                    bridge.send(publish2_packet)
                    if mosq_test.expect_packet(bridge, "puback", puback2_packet):
                        rc = 0

    bridge.close()
finally:
    os.remove(conf_file)
    try:
        bridge.close()
    except NameError:
        pass

    broker.terminate()
    broker.wait()
    (stdo, stde) = broker.communicate()
    if rc:
        print(stde)
    ssock.close()

exit(rc)


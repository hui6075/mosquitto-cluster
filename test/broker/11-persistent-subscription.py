#!/usr/bin/env python

# Test whether a client subscribed to a topic receives its own message sent to that topic.

import inspect, os, sys
# From http://stackoverflow.com/questions/279237/python-import-a-module-from-a-folder
cmd_subfolder = os.path.realpath(os.path.abspath(os.path.join(os.path.split(inspect.getfile( inspect.currentframe() ))[0],"..")))
if cmd_subfolder not in sys.path:
    sys.path.insert(0, cmd_subfolder)

import mosq_test

def write_config(filename, port):
    with open(filename, 'w') as f:
        f.write("port %d\n" % (port))
        f.write("persistence true\n")
        f.write("persistence_file mosquitto-%d.db\n" % (port))

port = mosq_test.get_port()
conf_file = os.path.basename(__file__).replace('.py', '.conf')
write_config(conf_file, port)

rc = 1
mid = 530
keepalive = 60
connect_packet = mosq_test.gen_connect(
    "persitent-subscription-test", keepalive=keepalive, clean_session=False,
)
connack_packet = mosq_test.gen_connack(rc=0)
connack_packet2 = mosq_test.gen_connack(rc=0, resv=1)  # session present

subscribe_packet = mosq_test.gen_subscribe(mid, "subpub/qos1", 1)
suback_packet = mosq_test.gen_suback(mid, 1)

mid = 300
publish_packet = mosq_test.gen_publish("subpub/qos1", qos=1, mid=mid, payload="message")
puback_packet = mosq_test.gen_puback(mid)

mid = 1
publish_packet2 = mosq_test.gen_publish("subpub/qos1", qos=1, mid=mid, payload="message")

if os.path.exists('mosquitto-%d.db' % (port)):
    os.unlink('mosquitto-%d.db' % (port))

broker = mosq_test.start_broker(filename=os.path.basename(__file__), use_conf=True, port=port)

(stdo1, stde1) = ("", "")
try:
    sock = mosq_test.do_client_connect(connect_packet, connack_packet, timeout=20, port=port)
    sock.send(subscribe_packet)

    if mosq_test.expect_packet(sock, "suback", suback_packet):
        broker.terminate()
        broker.wait()
        (stdo1, stde1) = broker.communicate()
        sock.close()
        broker = mosq_test.start_broker(filename=os.path.basename(__file__), use_conf=True, port=port)

        sock = mosq_test.do_client_connect(connect_packet, connack_packet2, timeout=20, port=port)

        sock.send(publish_packet)

        if mosq_test.expect_packet(sock, "puback", puback_packet):

            if mosq_test.expect_packet(sock, "publish2", publish_packet2):
                rc = 0

    sock.close()
finally:
    os.remove(conf_file)
    broker.terminate()
    broker.wait()
    (stdo, stde) = broker.communicate()
    if rc:
        print(stde1 + stde)
    if os.path.exists('mosquitto-%d.db' % (port)):
        os.unlink('mosquitto-%d.db' % (port))


exit(rc)


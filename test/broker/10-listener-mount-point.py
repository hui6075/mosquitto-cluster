#!/usr/bin/env python

import subprocess

import inspect, os, sys
# From http://stackoverflow.com/questions/279237/python-import-a-module-from-a-folder
cmd_subfolder = os.path.realpath(os.path.abspath(os.path.join(os.path.split(inspect.getfile( inspect.currentframe() ))[0],"..")))
if cmd_subfolder not in sys.path:
    sys.path.insert(0, cmd_subfolder)

import mosq_test

def write_config(filename, port1, port2):
    with open(filename, 'w') as f:
        f.write("port %d\n" % (port1))
        f.write("\n")
        f.write("listener %d\n" % (port2))
        f.write("mount_point mount/\n")
        f.write("\n")
        f.write("log_type debug\n")

(port1, port2) = mosq_test.get_port(2)
conf_file = os.path.basename(__file__).replace('.py', '.conf')
write_config(conf_file, port1, port2)

rc = 1
keepalive = 60
connect_packet = mosq_test.gen_connect("test2", keepalive=keepalive)
connack_packet = mosq_test.gen_connack(rc=0)

mid = 1
subscribe_packet = mosq_test.gen_subscribe(mid, "#", 0)
suback_packet = mosq_test.gen_suback(mid, 0)

publish_packet = mosq_test.gen_publish("mount/test", qos=0, payload="mount point")

broker = mosq_test.start_broker(filename=os.path.basename(__file__), use_conf=True, port=port1)

try:
    sock = mosq_test.do_client_connect(connect_packet, connack_packet, timeout=20, port=port1)
    sock.send(subscribe_packet)

    if mosq_test.expect_packet(sock, "suback", suback_packet):
        pub = subprocess.Popen(['./10-listener-mount-point-helper.py', str(port2)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        pub.wait()
        (stdo, stde) = pub.communicate()
        # Should have now received a publish command

        if mosq_test.expect_packet(sock, "publish", publish_packet):
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


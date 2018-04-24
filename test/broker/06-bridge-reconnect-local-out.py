#!/usr/bin/env python

# Test whether a bridge topics work correctly after reconnection.
# Important point here is that persistence is enabled.

import subprocess
import time

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
        f.write("persistence true\n")
        f.write("persistence_file mosquitto-%d.db" % (port1))
        f.write("\n")
        f.write("connection bridge_sample\n")
        f.write("address 127.0.0.1:%d\n" % (port1))
        f.write("topic bridge/# out\n")

(port1, port2) = mosq_test.get_port(2)
conf_file = '06-bridge-reconnect-local-out.conf'
write_config(conf_file, port1, port2)

rc = 1
keepalive = 60
connect_packet = mosq_test.gen_connect("bridge-reconnect-test", keepalive=keepalive)
connack_packet = mosq_test.gen_connack(rc=0)

mid = 180
subscribe_packet = mosq_test.gen_subscribe(mid, "bridge/#", 0)
suback_packet = mosq_test.gen_suback(mid, 0)
publish_packet = mosq_test.gen_publish("bridge/reconnect", qos=0, payload="bridge-reconnect-message")

try:
    os.remove('mosquitto-%d.db' % (port1))
except OSError:
    pass

broker = mosq_test.start_broker(filename=os.path.basename(__file__), port=port1, use_conf=False)

local_cmd = ['../../src/mosquitto', '-c', '06-bridge-reconnect-local-out.conf']
local_broker = mosq_test.start_broker(cmd=local_cmd, filename=os.path.basename(__file__)+'_local1', use_conf=False, port=port2)
if os.environ.get('MOSQ_USE_VALGRIND') is not None:
    time.sleep(5)
else:
    time.sleep(0.5)
local_broker.terminate()
local_broker.wait()
if os.environ.get('MOSQ_USE_VALGRIND') is not None:
    time.sleep(5)
else:
    time.sleep(0.5)
local_broker = mosq_test.start_broker(cmd=local_cmd, filename=os.path.basename(__file__)+'_local2', port=port2)
if os.environ.get('MOSQ_USE_VALGRIND') is not None:
    time.sleep(5)
else:
    time.sleep(0.5)

pub = None
try:
    sock = mosq_test.do_client_connect(connect_packet, connack_packet, port=port1)
    sock.send(subscribe_packet)

    if mosq_test.expect_packet(sock, "suback", suback_packet):
        sock.send(subscribe_packet)

        if mosq_test.expect_packet(sock, "suback", suback_packet):
            pub = subprocess.Popen(['./06-bridge-reconnect-local-out-helper.py', str(port2)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            pub.wait()
            (stdo, stde) = pub.communicate()
            # Should have now received a publish command

            if mosq_test.expect_packet(sock, "publish", publish_packet):
                rc = 0
    sock.close()
finally:
    os.remove(conf_file)
    time.sleep(1)
    broker.terminate()
    broker.wait()
    (stdo, stde) = broker.communicate()
    if rc:
        print(stde)
    local_broker.terminate()
    local_broker.wait()
    if rc:
        (stdo, stde) = local_broker.communicate()
        print(stde)
        if pub:
            (stdo, stde) = pub.communicate()
            print(stdo)

    try:
        os.remove('mosquitto-%d.db' % (port1))
    except OSError:
        pass

exit(rc)


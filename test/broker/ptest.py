#!/usr/bin/env python3

import subprocess
import time
import sys

max_running = 10
tests = [
    #(ports required, 'path'),
	(1, './01-connect-success.py'),
	(1, './01-connect-invalid-protonum.py'),
	(1, './01-connect-invalid-id-0.py'),
	(1, './01-connect-invalid-id-0-311.py'),
	(1, './01-connect-invalid-id-missing.py'),
	(1, './01-connect-invalid-reserved.py'),
	(1, './01-connect-invalid-id-utf8.py'),
	(1, './01-connect-anon-denied.py'),
	(1, './01-connect-uname-no-password-denied.py'),
	(1, './01-connect-uname-password-denied.py'),
	(1, './01-connect-uname-password-success.py'),
	(1, './01-connect-uname-no-flag.py'),
	(1, './01-connect-uname-pwd-no-flag.py'),
	(1, './01-connect-uname-invalid-utf8.py'),

	(1, './02-subscribe-qos0.py'),
	(1, './02-subscribe-qos1.py'),
	(1, './02-subscribe-qos2.py'),
	(1, './02-subpub-qos0.py'),
	(1, './02-subpub-qos1.py'),
	(1, './02-subpub-qos2.py'),
	(1, './02-unsubscribe-qos0.py'),
	(1, './02-unsubscribe-qos1.py'),
	(1, './02-unsubscribe-qos2.py'),
	(1, './02-unsubscribe-invalid-no-topic.py'),
	(1, './02-subscribe-invalid-utf8.py'),

	(1, './03-publish-qos1.py'),
	(1, './03-publish-qos2.py'),
	(1, './03-publish-b2c-disconnect-qos1.py'),
	(1, './03-publish-c2b-disconnect-qos2.py'),
	(1, './03-publish-b2c-disconnect-qos2.py'),
	(1, './03-pattern-matching.py'),
	(1, './03-publish-qos1-queued-bytes.py'),
	(1, './03-publish-invalid-utf8.py'),

	(1, './04-retain-qos0.py'),
	(1, './04-retain-qos0-fresh.py'),
	(1, './04-retain-qos0-repeated.py'),
	(1, './04-retain-qos1-qos0.py'),
	(1, './04-retain-qos0-clear.py'),
	(1, './04-retain-upgrade-outgoing-qos.py'),

	(1, './05-clean-session-qos1.py'),

	(2, './06-bridge-reconnect-local-out.py'),
	(2, './06-bridge-br2b-disconnect-qos1.py'),
	(2, './06-bridge-br2b-disconnect-qos2.py'),
	(2, './06-bridge-b2br-disconnect-qos1.py'),
	(2, './06-bridge-b2br-disconnect-qos2.py'),
	(2, './06-bridge-fail-persist-resend-qos1.py'),
	(2, './06-bridge-fail-persist-resend-qos2.py'),
	(2, './06-bridge-b2br-remapping.py'),
	(2, './06-bridge-br2b-remapping.py'),

	(1, './07-will-qos0.py'),
	(1, './07-will-null.py'),
	(1, './07-will-null-topic.py'),
	(1, './07-will-invalid-utf8.py'),
	(1, './07-will-no-flag.py'),

	(2, './08-ssl-connect-no-auth.py'),
	(2, './08-ssl-connect-no-auth-wrong-ca.py'),
	(2, './08-ssl-connect-cert-auth.py'),
	(2, './08-ssl-connect-cert-auth-without.py'),
	(2, './08-ssl-connect-cert-auth-expired.py'),
	(2, './08-ssl-connect-cert-auth-revoked.py'),
	(2, './08-ssl-connect-cert-auth-crl.py'),
	(2, './08-ssl-connect-identity.py'),
	(2, './08-ssl-connect-no-identity.py'),
	(2, './08-ssl-bridge.py'),
	(2, './08-tls-psk-pub.py'),
	(3, './08-tls-psk-bridge.py'),

	(1, './09-plugin-auth-unpwd-success.py'),
	(1, './09-plugin-auth-unpwd-fail.py'),
	(1, './09-plugin-auth-acl-sub.py'),
	(1, './09-plugin-auth-v2-unpwd-success.py'),
	(1, './09-plugin-auth-v2-unpwd-fail.py'),
	(1, './09-plugin-auth-defer-unpwd-success.py'),
	(1, './09-plugin-auth-defer-unpwd-fail.py'),

	(2, './10-listener-mount-point.py'),

	(1, './11-persistent-subscription.py'),
    ]

minport = 1888
ports = list(range(minport, minport+max_running+1))

def next_test(tests, ports):
    if len(tests) == 0 or len(ports) == 0:
        return

    test = tests.pop()
    if test[0] == 1:
        port = ports.pop()
        p = subprocess.Popen([test[1], str(port)])
        p.mosq_port = port
        return p
    elif test[0] == 2:
        if len(ports) < 2:
            tests.insert(0,test)
            return None
        else:
            port1 = ports.pop()
            port2 = ports.pop()

            p = subprocess.Popen([test[1], str(port1), str(port2)])
            p.mosq_port = (port1, port2)
            return p
    elif test[0] == 3:
        if len(ports) < 3:
            tests.insert(0,test)
            return None
        else:
            port1 = ports.pop()
            port2 = ports.pop()
            port3 = ports.pop()

            p = subprocess.Popen([test[1], str(port1), str(port2), str(port3)])
            p.mosq_port = (port1, port2, port3)
            return p
    else:
        return None


def run_tests(tests, ports):
    passed = 0
    failed = 0

    failed_tests = []

    running_tests = []
    while len(tests) > 0 or len(running_tests) > 0:
        if len(running_tests) <= max_running:
            t = next_test(tests, ports)
            if t is None:
                time.sleep(0.1)
            else:
                running_tests.append(t)

        for t in running_tests:
            t.poll()
            if t.returncode is not None:
                running_tests.remove(t)
                if isinstance(t.mosq_port, tuple):
                    for portret in t.mosq_port:
                        ports.append(portret)
                else:
                    ports.append(t.mosq_port)
                t.terminate()
                t.wait()
                #(stdo, stde) = t.communicate()
                if t.returncode == 1:
                    print("\033[31m" + t.args[0] + "\033[0m")
                    failed = failed + 1
                else:
                    passed = passed + 1
                    print("\033[32m" + t.args[0] + "\033[0m")

    print("Passed: %d\nFailed: %d\nTotal: %d" % (passed, failed, passed+failed))
    if failed > 0:
        sys.exit(1)


run_tests(tests, ports)

#!/usr/bin/env python3

import subprocess
import time
import sys

max_running = 10
tests = [
    ('./01-con-discon-success.py', 'c/01-con-discon-success.test'),
    ('./01-keepalive-pingreq.py', 'c/01-keepalive-pingreq.test'),
    ('./01-no-clean-session.py', 'c/01-no-clean-session.test'),
    ('./01-unpwd-set.py', 'c/01-unpwd-set.test'),
    ('./01-will-set.py', 'c/01-will-set.test'),
    ('./01-will-unpwd-set.py', 'c/01-will-unpwd-set.test'),
    ('./02-subscribe-qos0.py', 'c/02-subscribe-qos0.test'),
    ('./02-subscribe-qos1.py', 'c/02-subscribe-qos1.test'),
    ('./02-subscribe-qos2.py', 'c/02-subscribe-qos2.test'),
    ('./02-unsubscribe.py', 'c/02-unsubscribe.test'),
    ('./03-publish-b2c-qos1.py', 'c/03-publish-b2c-qos1.test'),
    ('./03-publish-b2c-qos2.py', 'c/03-publish-b2c-qos2.test'),
    ('./03-publish-c2b-qos1-disconnect.py', 'c/03-publish-c2b-qos1-disconnect.test'),
    ('./03-publish-c2b-qos2-disconnect.py', 'c/03-publish-c2b-qos2-disconnect.test'),
    ('./03-publish-c2b-qos2.py', 'c/03-publish-c2b-qos2.test'),
    ('./03-publish-qos0-no-payload.py', 'c/03-publish-qos0-no-payload.test'),
    ('./03-publish-qos0.py', 'c/03-publish-qos0.test'),
    ('./04-retain-qos0.py', 'c/04-retain-qos0.test'),
    ('./08-ssl-bad-cacert.py', 'c/08-ssl-bad-cacert.test'),
    ('./08-ssl-connect-cert-auth-enc.py', 'c/08-ssl-connect-cert-auth-enc.test'),
    ('./08-ssl-connect-cert-auth.py', 'c/08-ssl-connect-cert-auth.test'),
    ('./08-ssl-connect-no-auth.py', 'c/08-ssl-connect-no-auth.test'),
    ('./09-util-topic-matching.py', 'c/09-util-topic-matching.test'),
    ('./09-util-topic-tokenise.py', 'c/09-util-topic-tokenise.test'),
    ('./09-util-utf8-validate.py', 'c/09-util-utf8-validate.test'),

    ('./01-con-discon-success.py', 'cpp/01-con-discon-success.test'),
    ('./01-keepalive-pingreq.py', 'cpp/01-keepalive-pingreq.test'),
    ('./01-no-clean-session.py', 'cpp/01-no-clean-session.test'),
    ('./01-unpwd-set.py', 'cpp/01-unpwd-set.test'),
    ('./01-will-set.py', 'cpp/01-will-set.test'),
    ('./01-will-unpwd-set.py', 'cpp/01-will-unpwd-set.test'),
    ('./02-subscribe-qos0.py', 'cpp/02-subscribe-qos0.test'),
    ('./02-subscribe-qos1.py', 'cpp/02-subscribe-qos1.test'),
    ('./02-subscribe-qos2.py', 'cpp/02-subscribe-qos2.test'),
    ('./02-unsubscribe.py', 'cpp/02-unsubscribe.test'),
    ('./03-publish-b2c-qos1.py', 'cpp/03-publish-b2c-qos1.test'),
    ('./03-publish-b2c-qos2.py', 'cpp/03-publish-b2c-qos2.test'),
    ('./03-publish-c2b-qos1-disconnect.py', 'cpp/03-publish-c2b-qos1-disconnect.test'),
    ('./03-publish-c2b-qos2-disconnect.py', 'cpp/03-publish-c2b-qos2-disconnect.test'),
    ('./03-publish-c2b-qos2.py', 'cpp/03-publish-c2b-qos2.test'),
    ('./03-publish-qos0-no-payload.py', 'cpp/03-publish-qos0-no-payload.test'),
    ('./03-publish-qos0.py', 'cpp/03-publish-qos0.test'),
    ('./04-retain-qos0.py', 'cpp/04-retain-qos0.test'),
    ('./08-ssl-bad-cacert.py', 'cpp/08-ssl-bad-cacert.test'),
    ('./08-ssl-connect-cert-auth-enc.py', 'cpp/08-ssl-connect-cert-auth-enc.test'),
    ('./08-ssl-connect-cert-auth.py', 'cpp/08-ssl-connect-cert-auth.test'),
    ('./08-ssl-connect-no-auth.py', 'cpp/08-ssl-connect-no-auth.test'),
    ('./09-util-topic-matching.py', 'cpp/09-util-topic-matching.test'),
    ('./09-util-topic-tokenise.py', 'cpp/09-util-topic-tokenise.test'),
    ('./09-util-utf8-validate.py', 'cpp/09-util-utf8-validate.test'),
    ]

minport = 1888
ports = list(range(minport, minport+max_running+1))

def next_test(tests, ports):
    if len(tests) == 0 or len(ports) == 0:
        return

    test = tests.pop()
    port = ports.pop()
    p = subprocess.Popen([test[0], test[1], str(port)])
    p.mosq_port = port
    return p

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
                    print("\033[31m %s %s\033[0m" % (t.args[0], t.args[1]))
                    failed = failed + 1
                else:
                    passed = passed + 1
                    print("\033[32m %s %s\033[0m" % (t.args[0], t.args[1]))

    print("Passed: %d\nFailed: %d\nTotal: %d" % (passed, failed, passed+failed))
    if failed > 0:
        sys.exit(1)


run_tests(tests, ports)

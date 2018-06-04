Mosquitto with cluster
=================

[中文版自述](https://github.com/hui6075/mosquitto-cluster/blob/master/readme-zh_cn.md)<br>
In a mosquitto cluster, clients can subscribe to every node, and can also publish to every other node. The cluster will make sure that published messages are forwarded as needed.<br>
The cluster is full decentralized, autonomy system without any leader or key node, to make the system with a high availablity.
E.g., each node has a fault rate with 1%, then a decentralized cluster with N nodes has a service availability which is 1-1%^N.<br>

## Usage

Install mosquitto on all of the nodes and write the addresses into mosquitto.conf, e.g.,<br>
```
node_name node1
node_address 192.168.1.1:1883

node_name node2
node_address 192.168.1.2:1883
```

Then config the loadbalancer, take above adresses as real server address. It is strongly recommend to terminate TLS on the loadbalancer, and use plain TCP inside the cluster.<br>

## Installing
\> git clone https://github.com/hui6075/mosquitto-cluster.git </br>
\> cd mosquitto-cluster && vi config.mk </br>
```
# WITH_BRIDGE:=yes
WITH_CLUSTER:=yes
```
\> make && make install </br>

## Cluster Specification

### Traffic cycle avoid
In order to avoid infinite PUB/SUB forwarding, the publishes from other brokers will only send to client, the subscription and unsubscription from other brokers will not be forward.<br>
### Duplicate subscription avoid
Save local clients' subscriptions and unsubscriptions by a reference counter, only forward the subscription which is fresh for local broker, and only forward the unsubscription which is no longger been subscribed by local client.<br>
### Private messages
Some private messages was introduced in order to support cluster session and retain message, i.e.,<br>
PRIVATE SUBSCRIBE<br>
Fix header|PacketID|Topic Filter|QoS|ClientID|SubID<br><br>
PRIVATE RETAIN<br>
Fix header|Topic|QoS|[PacketID]|ClientID|SubID|OriginalRecvTime|Payload<br><br>
SESSION REQ<br>
Fix header|ClientID|Clean Session<br><br>
SESSION RESP<br>
Fix header|ClientID|PacketID|NRsubs|sub1(topic|qos)|...|subN|NRpubs|pub1(topic|state|dir|dup|qos|mid|payload)|...|pubN|<br>
### Cluster session support
For cluster session, SESSION REQ would be broadcast for each client CONNECT which has no previous context found in local broker, remote broker which has this client's context will kick-off this client and if clean session set to false, the remote broker would return this client's session include subscription, incomplete publishes with QoS=0/1 inside SESSION RESP. This feature could be disable in mosquitto.conf, in order to endure instantaneous large number of concurrent connections at system startup phase.<br>
### Cluster retain message support
For retain message, PRIVATE SUBSCRIBE would be broadcast for each client subscription that
is fresh for local broker, and if there exists a retain message, remote broker would
return the retain message inside PRIVATE RETAIN. Client will receive the most recent retain message inside the cluster after a frozen window which can be configure inside mosquitto.conf.<br>
### QoS support
For QoS, all the publishes set with it's original QoS inside the cluster, and process with QoS=0, to saves the inside traffic. The actual QoS send to client decide by the broker which client connected with.<br>
### Other features
All validation, utf-8, ACL checking has disabled for the messages inside the cluster, in order to improve cluster efficiency.<br><br>
Once CONNECT success with other broker, local broker will send all the local clients' subscription to remote broker, to avoid subscription loss while some brokers down and up.<br><br>
The cluster current also support node/subscription recover, crash detection.<br>
## Design Philosophy
### Mosquitto cluster goals
Mosquitto cluster is a distributed implementation of Mosquitto brokers with following goals:<br>
Scalable in a horizontal fashion, which means you can expand cluster capability by increase number of brokers.<br>
Provide continuous service under single point of failure.<br>
Take on the role of one logical MQTT broker for millions of MQTT clients.<br>

![image](https://github.com/hui6075/mosquitto/blob/develop/img/1.jpg)
<p align="center"><b>Pic1. Mosquitto Cluster Overview</b></p>
While receive client subscriptin: Notification other brokers that the topic has been subscribed by me,<br>
While receive client publish: route the message to the correct brokers which subscribed this topic.<br>

![image](https://github.com/hui6075/mosquitto/blob/develop/img/2.jpg)
<p align="center"><b>Pic2. Mosquitto Cluster Private Messages</b></p>
There's only one logic channel between 2 brokers, in order to reuse this channel for multi clients, 
some private messages has been introduced, include client id, subscription id, raw timestamp and etc., 
which help the broker to make a correct route decision.<br>

![image](https://github.com/hui6075/mosquitto/blob/develop/img/3.jpg)
<p align="center"><b>Pic3. Mosquitto Cluster Internal Message Flow</b></p>
Forwarding rules:<br>
Forward local client subscriptions to the broker which is a fresh guy for the cluster or just recovered from a fault.<br>
DO NOT forward internal messages to any broker.<br>
Only broadcast local fresh subscription to other brokers.<br>
Broadcast unsubscription until a topic is no longer subscribed by any local client.<br>
Session messages broadcasting can be disable by configuration.<br>
DO NOT forward PUB/SUBs under $SYS.

## Benchmark
Using [krylovsk/mqtt-benchmark](https://github.com/krylovsk/mqtt-benchmark) as the benchmark tool to give a simply testing for the cluster. The bandwidth stop increase with 3 or more brokers due to client machine's bottleneck.<br>
![image](https://github.com/hui6075/mosquitto/blob/develop/img/cluster_throughput.jpg)
<p align="center"><b>Pic4. Mean bandwidth of Mosquitto cluster</b></p>
n=10k means 10000 messages to send per client, c=100 means 100 clients to start.
MsgSize=1000bytes, QoS=2.<br>

A more detailed test report is available under:
https://github.com/hui6075/mosquitto/tree/develop/benchmark

## Study Material

![image](https://github.com/hui6075/mosquitto-cluster/blob/master/benchmark/mosquitto_code_analyze.jpg)
<p align="center"><b>Pic5. Mosquitto code analyze(benchmark/mosquitto_code_analyze.jpg, w/o cluster)</b></p>

## Other Projects
* MQTT v5.0 Chinese Translation: <https://github.com/hui6075/mqtt_v5>
* MQTT broker forward latency measurement tool: <https://github.com/hui6075/mqtt-bm-latency>

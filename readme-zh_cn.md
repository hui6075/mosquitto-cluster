Mosquitto集群
=================

在Mosquitto集群中，客户端可以在任何节点上订阅主题，也可以在任何节点上发布消息，集群会保证消息按需转发到正确的节点。<br>
为了均衡负载及避免单点故障，Mosquitto集群实现为完全去中心化、自治的方式。<br>

## 编译安装

\> git clone https://github.com/hui6075/mosquitto-cluster.git </br>
\> cd mosquitto-cluster && vi config.mk </br>
```
# WITH_BRIDGE:=yes
WITH_CLUSTER:=yes
```
\> make && make install </br>

## 部署

在所有节点上安装Mosquitto，并把节点名、IP、端口号写进配置文件mosquitto.conf，例如：<br>
`````
node_name node1
node_address 192.168.1.1:1883

node_name node2
node_address 192.168.1.2:1883
`````
然后配置负载均衡器，把所有节点的地址：端口号作为后端服务地址。Mosquitto是单进程实现，建议单机多实例部署，并把TLS终结在负载均衡器。

## Mosquitto集群特性

客户端的连接/订阅/取消订阅消息广播到集群内其他节点，发布消息按需转发。<br>
#### 环路避免
任何节点对于来自其他节点的发布消息只会发送给客户端，对于来自其他节点的订阅/取消订阅消息不会被转发。<br>
#### 避免重复订阅
对于每个来自客户端的订阅主题，以引用计数的方式保存在本地，只有在收到新的客户端主题订阅消息，或者主题不再被任何客户端订阅时进行转发。<br>
#### 私有消息
为支持集群session及保留消息，Mosquitto集群引入了如下私有消息：<br>
PRIVATE SUBSCRIBE<br>
固定头部|报文标识符|主题过滤器|QoS|客户端标识符|订阅标识符<br><br>
PRIVATE RETAIN<br>
固定头部|主题过滤器|QoS|[报文标识符]|客户端标识符|订阅标识符|接收时间戳|有效载荷<br><br>
SESSION REQ<br>
固定头部|客户端标识符|会话清除标识<br><br>
SESSION RESP<br>
固定头部|客户端标识符|报文标识符|订阅数量|订阅1(主题过滤器|QoS)|...|订阅N|发布消息数量|发布消息1(主题过滤器|状态|方向|重复标识|QoS|报文标识符|有效载荷)|...|发布消息N|<br>
#### 集群会话支持
每次客户端连接时，若本地没有发现会话信息，则广播SESSION REQ消息，其他节点收到此请求时若发现此客户端的会话信息，则断开/清除此客户端/上下文，若会话清除标识被设为FALSE，则返回此客户端的订阅状态及未完成的QoS为0和1的发布消息。此特性可以通过配置文件禁用，但压测和性能采样结果表明此特性在大并发的情况下并不会带来特别大的开销。<br>
#### 集群保留消息支持
每次收到来自于客户端的订阅时，节点会广播PRIVATE SUBSCRIBE，如果存在保留消息，其他节点会把此保留消息及接收时间通过PRIVATE RETAIN消息返回给此节点，因此客户端得以收到集群中绝对时间戳最晚的保留消息。<br>
#### QoS支持
所有发布消息以原始QoS在集群内转发，但被节点当做QoS为0进行处理。实际转发给客户端的发布消息按照协议取发布QoS和订阅QoS两者较小值。<br>
#### 其他特性
集群间消息处理时不进行校验。集群间建立连接时发送所有本地先前的订阅关系。<br>
## 设计思想
#### Mosquitto集群目标
水平扩展，避免单点故障，整个集群对外表现为一个完整的逻辑MQTT代理。<br>

![image](https://github.com/hui6075/mosquitto/blob/develop/img/1.jpg)
##### <div align=center>Pic1. Mosquitto集群概览<br></div>
每次收到客户端的订阅消息时，通知其他节点此话题被订阅；每次收到客户端的发布消息时，把消息路由到正确的节点上。<br>

![image](https://github.com/hui6075/mosquitto/blob/develop/img/2.jpg)
##### <div align=center>Pic2. Mosquitto集群私有消息<br></div>
节点与其他节点间只有一条逻辑通道，为了承载不同客户端的订阅/发布消息，引入了一些私有消息，包含客户端标识符、订阅标识符、时间戳等等，以帮助节点做出正确的路由和转发。<br>

![image](https://github.com/hui6075/mosquitto/blob/develop/img/3.jpg)
##### <div align=center>Pic3. Mosquitto集群内部消息流<br></div>
转发规则：<br>
转发来自于客户端的本节点新发生的订阅；当话题不再被任何客户端订阅时广播取消订阅消息；不转发任何系统话题。<br>

## Benchmark
使用[krylovsk/mqtt-benchmark](https://github.com/krylovsk/mqtt-benchmark)对集群进行简单的基准测试，集群吞吐率在3个以上节点时受限于客户端从而停止增长。<br>
![image](https://github.com/hui6075/mosquitto/blob/develop/img/cluster_throughput.jpg)
##### <div align=center>Pic4. Mosquitto集群平均吞吐率<br></div>
n=10k表示每个客户端发送10000条消息, c=100表示总共启动100个客户端。<br>
消息长度1000字节，QoS选取为2。<br>

更详细的Tsung压测报告：
https://github.com/hui6075/mosquitto-cluster/tree/master/benchmark

## 其他
![image](https://github.com/hui6075/mosquitto-cluster/blob/master/benchmark/mosquitto_code_analyze.jpg)
##### <div align=center>Pic5. Mosquitto源码分析<br></div>

## 开源项目
* MQTT v5.0协议草案中文版翻译: <https://github.com/hui6075/mqtt_v5>
* MQTT broker转发延时压测工具: <https://github.com/hui6075/mqtt-bm-latency>

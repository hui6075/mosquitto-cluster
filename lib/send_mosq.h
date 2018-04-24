/*
Copyright (c) 2010-2018 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License v1.0
and Eclipse Distribution License v1.0 which accompany this distribution.
 
The Eclipse Public License is available at
   http://www.eclipse.org/legal/epl-v10.html
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.
 
Contributors:
   Roger Light - initial implementation and documentation.
*/
#ifndef SEND_MOSQ_H
#define SEND_MOSQ_H

#include "mosquitto.h"

int send__simple_command(struct mosquitto *mosq, uint8_t command);
int send__command_with_mid(struct mosquitto *mosq, uint8_t command, uint16_t mid, bool dup);
int send__real_publish(struct mosquitto *mosq, uint16_t mid, const char *topic, uint32_t payloadlen, const void *payload, int qos, bool retain, bool dup);

int send__connect(struct mosquitto *mosq, uint16_t keepalive, bool clean_session);
int send__disconnect(struct mosquitto *mosq);
int send__pingreq(struct mosquitto *mosq);
int send__pingresp(struct mosquitto *mosq);
int send__puback(struct mosquitto *mosq, uint16_t mid);
int send__pubcomp(struct mosquitto *mosq, uint16_t mid);
int send__publish(struct mosquitto *mosq, uint16_t mid, const char *topic, uint32_t payloadlen, const void *payload, int qos, bool retain, bool dup);
int send__pubrec(struct mosquitto *mosq, uint16_t mid);
int send__pubrel(struct mosquitto *mosq, uint16_t mid);
int send__subscribe(struct mosquitto *mosq, int *mid, const char *topic, uint8_t topic_qos);
int send__unsubscribe(struct mosquitto *mosq, int *mid, const char *topic);

#ifdef WITH_CLUSTER
int send__private_subscribe(struct mosquitto *context, int *mid, const char *topic, uint8_t topic_qos, char *client_id, uint16_t sub_id);
int send__private_retain(struct mosquitto *context, char *remote_client_id, uint16_t sub_id, const char* topic, uint8_t qos, int mid, int64_t rcv_time, uint32_t payloadlen, const void *payload);
int send__session_req(struct mosquitto *node, char *client_id, uint8_t clean_session);
int send__session_resp(struct mosquitto *peer, char *client_id, struct mosquitto *client_context);
int send__multi_subscribes(struct mosquitto *context, int *mid, char **topic_arr, int topic_arr_len);
int send__multi_unsubscribe(struct mosquitto *context, int *mid, char **topic_arr, int topic_arr_len);
#endif

#endif

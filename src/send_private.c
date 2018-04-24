/*
Copyright (c) 2009-2018 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License v1.0
and Eclipse Distribution License v1.0 which accompany this distribution.

The Eclipse Public License is available at
   http://www.eclipse.org/legal/epl-v10.html
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.

Contributors:
   Zhan Jianhui - Simple implementation cluster.
*/

#include "config.h"
#include <assert.h>

#include "mosquitto_broker.h"
#include "mqtt3_protocol.h"
#include "memory_mosq.h"
#include "util_mosq.h"
#include "packet_mosq.h"

#ifdef WITH_CLUSTER
int send__private_subscribe(struct mosquitto *context, int *mid, const char *topic, uint8_t topic_qos, char *client_id, uint16_t sub_id)
{
	/* FIXME - only deals with a single topic */
	struct mosquitto__packet *packet = NULL;
	uint32_t packetlen;
	uint16_t local_mid;
	int rc;

	if(!context->is_node)
		return MOSQ_ERR_INVAL;

	packet = mosquitto__calloc(1, sizeof(struct mosquitto__packet));
	if(!packet) return MOSQ_ERR_NOMEM;

	packetlen = 2/* 0. Message Identifier */ + 2/* 1. topic length */ +strlen(topic) + 1/* 2. QoS */ + 
                2/* 3. client id length*/ + strlen(client_id) + 2/* 4. sub_id */;

	packet->command = PRIVATE | PRIVATE_SUBSCRIBE;
	packet->remaining_length = packetlen;
	rc = packet__alloc(packet);
	if(rc){
		mosquitto__free(packet);
		return rc;
	}

	/* Variable header */
	local_mid = mosquitto__mid_generate(context);
	if(mid)
		*mid = (int)local_mid;
	/* 0. mid */
	packet__write_uint16(packet, local_mid);
	/* 1. topic */
	packet__write_string(packet, topic, strlen(topic));
	/* 2. QoS */
	packet__write_byte(packet, topic_qos);
	/* 3. original client id */
	packet__write_string(packet, client_id, strlen(client_id));
	/* 4. sub id */
	packet__write_uint16(packet, sub_id);

	log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Sending private subscribe to node:%s (client_id:%s subid:%d, q%d, m%d, '%s')",
								      context->id, client_id, sub_id, topic_qos, local_mid, topic);

	return packet__queue(context, packet);
}

int send__private_retain(struct mosquitto *context, char *remote_client_id, uint16_t sub_id, const char* topic, uint8_t qos, int mid, int64_t rcv_time, uint32_t payloadlen, const void *payload)
{
	int packetlen;
	int rc;
	int64_t remote_rcv_time;
	char tmp_time_str[9] = {0};
	struct mosquitto__packet *packet = NULL;

	if(!context->is_peer || !topic)
		return MOSQ_ERR_INVAL;

	packetlen = 2/* 1. topic */ + strlen(topic) + 1/* 2. QoS*/ + 
		        2/* 4. remote client id */ + strlen(remote_client_id) + 2/* 5. sub id */ +
		        2/* 6. original rcv time */ + 8 + payloadlen;
	if(qos > 0) /* 3. message id */
		packetlen += 2;
	packet = mosquitto__calloc(1, sizeof(struct mosquitto__packet));
	if(!packet)
		return MOSQ_ERR_NOMEM;

	packet->mid = mid;
	packet->command = PRIVATE | PRIVATE_RETAIN;
	packet->remaining_length = packetlen;
	rc = packet__alloc(packet);
	if(rc){
		mosquitto__free(packet);
		return rc;
	}
	/* 1. topic*/
	packet__write_string(packet, topic, strlen(topic));

	/* 2. QoS */
	packet__write_byte(packet, qos);

	/* 3. mid */
	if(qos > 0){
		packet__write_uint16(packet, mid);
	}

	/* 4. remote client id */
	if(remote_client_id){
		packet__write_string(packet, remote_client_id, strlen(remote_client_id));
	}

	/* 5. sub id */
	packet__write_uint16(packet, sub_id);

	/* 6. Retain Rcv Time */
	remote_rcv_time = rcv_time + context->remote_time_offset;
	mosq_time_to_hexstr(remote_rcv_time, tmp_time_str);
	packet__write_string(packet, tmp_time_str, 8);

	/* 7. payload */
	if(payloadlen){
		packet__write_bytes(packet, payload, payloadlen);
	}

	log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Sending private retain to peer:%s (client_id:%s, subid:%d, q%d, m%d, '%s', ... (%ld bytes))",
		                              context->id, remote_client_id, sub_id, qos, mid, topic, (long)payloadlen);

	return packet__queue(context, packet);
}

int send__session_req(struct mosquitto *context, char *client_id, uint8_t clean_session)
{
	struct mosquitto__packet *packet = NULL;
	uint32_t packetlen;
	int rc;

	if(!context->is_node)
		return MOSQ_ERR_INVAL;

	packet = mosquitto__calloc(1, sizeof(struct mosquitto__packet));
	if(!packet) return MOSQ_ERR_NOMEM;

	packetlen = 2 + strlen(client_id) + 1;

	packet->command = PRIVATE | SESSION_REQ;
	packet->remaining_length = packetlen;
	rc = packet__alloc(packet);
	if(rc){
		mosquitto__free(packet);
		return rc;
	}

	/* client id */
	packet__write_string(packet, client_id, strlen(client_id));

	/* clean session */
	packet__write_byte(packet, clean_session);

	log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Sending session request to node: %s for client_id: %s",
										  context->id, client_id);

	return packet__queue(context, packet);
}


int send__session_resp(struct mosquitto *context, char *client_id, struct mosquitto *client_context)
{
	uint8_t flag, state, dir, dup, qos;
	uint16_t nrsubs = 0, nrpubs  =0;
	uint32_t packetlen;
	int rc, i, payload_len = 0;
	struct mosquitto__packet *packet;
	struct client_sub_table *client_sub;
	struct mosquitto_client_msg *clientmsg;
	struct mosquitto_msg_store *pubmsg ;

	if(!context->is_peer)
		return MOSQ_ERR_INVAL;

	packet = mosquitto__calloc(1, sizeof(struct mosquitto__packet));
	if(!packet) return MOSQ_ERR_NOMEM;

	/* client id + last_mid + 
	   num of subs + sub1(topic/qos) + sub2(...) + ... + 
	   num of pubs + pub1(topic/flag/mid/payload) + pub2(...) + ... */
	packetlen = 2 + strlen(client_id) + 2 + 2 + 2;

	packet->command = PRIVATE | SESSION_RESP;

	/* caculate total subs length and num of subs */
	for(i = 0; i < client_context->client_sub_count; i++){
		client_sub = client_context->client_subs[i];
		if(client_sub){
			payload_len += (2 + strlen(client_sub->sub_tbl->topic)+ 1);
			nrsubs++;
		}
	}

	clientmsg = client_context->inflight_msgs;
	while(clientmsg){
		if(clientmsg->qos > 0 && clientmsg->state != mosq_ms_invalid && clientmsg->state != mosq_ms_publish_qos0 && !clientmsg->retain){
			pubmsg = clientmsg->store;
			payload_len += 2 + strlen(pubmsg->topic) + 1 + 2 + 4 + pubmsg->payloadlen;
			nrpubs++;
		}
		clientmsg = clientmsg->next;
	}

	clientmsg = client_context->queued_msgs;
	while(clientmsg){
		if(clientmsg->qos > 0 && clientmsg->state != mosq_ms_invalid && clientmsg->state != mosq_ms_publish_qos0 && !clientmsg->retain){
			pubmsg = clientmsg->store;
			payload_len += 2 + strlen(pubmsg->topic) + 1 + 2 + 4 + pubmsg->payloadlen;
			nrpubs++;
		}
		clientmsg = clientmsg->next;
	}

	log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Sending session response to peer: %s for client: %s, mid:%d, total %d SUBs, %d PUBs",
										  context->id, client_context->id, client_context->last_mid, nrsubs, nrpubs);
	packet->remaining_length = packetlen + payload_len;

	rc = packet__alloc(packet);
	if(rc){
		mosquitto__free(packet);
		return rc;
	}

	/* client id */
	packet__write_string(packet, client_id, strlen(client_id));

	/* last mid */
	packet__write_uint16(packet, client_context->last_mid);

	/* num of subs */
	packet__write_uint16(packet, nrsubs);

	/* subs */
	for(i = 0; i < client_context->client_sub_count; i++){
		client_sub = client_context->client_subs[i];
		if(client_sub){
			packet__write_string(packet, client_sub->sub_tbl->topic, strlen(client_sub->sub_tbl->topic));
			packet__write_byte(packet, client_sub->sub_qos);

			log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER]\tSUBs: topic:%s(%zd bytes) qos:%d(out)",
										  client_sub->sub_tbl->topic, strlen(client_sub->sub_tbl->topic), client_sub->sub_qos);
		}
	}

	/* num of pubs */
	packet__write_uint16(packet, nrpubs);

	/* pubs */
	clientmsg = client_context->inflight_msgs;
	while(clientmsg){
		if(clientmsg->qos > 0 && clientmsg->state != mosq_ms_invalid && clientmsg->state != mosq_ms_publish_qos0 && !clientmsg->retain){
			pubmsg = clientmsg->store;
			packet__write_string(packet, pubmsg->topic, strlen(pubmsg->topic));
			/* flag = (state|dir|dup|qos) */
			state = (uint8_t)clientmsg->state;
			dir = (uint8_t)clientmsg->direction;
			dup = (uint8_t)clientmsg->dup;
			qos = (uint8_t)clientmsg->qos;
			flag = (state<<4) + (dir<<3) + (dup<<2) + qos;

			packet__write_byte(packet, flag);
			packet__write_uint16(packet, clientmsg->mid);
			packet__write_uint32(packet, pubmsg->payloadlen);

			packet__write_bytes(packet, UHPA_ACCESS_PAYLOAD(pubmsg), pubmsg->payloadlen);

			log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER]\tPUBs (d%d, q%d, s%d, m%d, '%s', ... (%ld bytes))(out)",
										  		dup, qos, state, clientmsg->mid, pubmsg->topic, (long)pubmsg->payloadlen);

			db__msg_store_deref(client_context->db, &pubmsg);
		}

		clientmsg = clientmsg->next;
	}

	clientmsg = client_context->queued_msgs;
	while(clientmsg){
		if(clientmsg->qos > 0 && clientmsg->state != mosq_ms_invalid && clientmsg->state != mosq_ms_publish_qos0 && !clientmsg->retain){
			pubmsg = clientmsg->store;
			packet__write_string(packet, pubmsg->topic, strlen(pubmsg->topic));
			state = (uint8_t)clientmsg->state;
			dir = (uint8_t)clientmsg->direction;
			dup = (uint8_t)clientmsg->dup;
			qos = (uint8_t)clientmsg->qos;
			flag = (state<<4) + (dir<<3) + (dup<<2) + qos;

			packet__write_byte(packet, flag);
			packet__write_uint16(packet, clientmsg->mid);
			packet__write_uint32(packet, pubmsg->payloadlen);
			packet__write_bytes(packet, UHPA_ACCESS_PAYLOAD(pubmsg), pubmsg->payloadlen);

			log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER]\tPubs (d%d, q%d, s%d, m%d, '%s', ... (%ld bytes))(out)",
										  		dup, qos, state, clientmsg->mid, pubmsg->topic, (long)pubmsg->payloadlen);

			db__msg_store_deref(client_context->db, &pubmsg);
		}
		clientmsg = clientmsg->next;
	}

	return packet__queue(context, packet);
}

int send__multi_subscribes(struct mosquitto *context, int *mid, char **topic_arr, int topic_arr_len)
{
	int rc, i;
	uint16_t local_mid;
	uint32_t packetlen;
	struct mosquitto__packet *packet;

	if(!context->is_node || !topic_arr || !topic_arr_len)
		return MOSQ_ERR_INVAL;

	packetlen = 2; /* mid */

	for(i=0; i<topic_arr_len; i++){/* topic + qos */
		packetlen += (2+strlen(topic_arr[i])+1);
	}

	packet = mosquitto__calloc(1, sizeof(struct mosquitto__packet));
	
	if(!packet)
		return MOSQ_ERR_NOMEM;

	packet->command = SUBSCRIBE | (1<<1);
	packet->remaining_length = packetlen;
	rc = packet__alloc(packet);
	if(rc){
		mosquitto__free(packet);
		return rc;
	}

	/* Variable header */
	local_mid = mosquitto__mid_generate(context);
	if(mid)
		*mid = (int)local_mid;
	packet__write_uint16(packet, local_mid);

	/* Payload */
	for(i=0; i<topic_arr_len; i++){
		packet__write_string(packet, topic_arr[i], strlen(topic_arr[i]));
		packet__write_byte(packet, 0);
	}

	log__printf(context, MOSQ_LOG_DEBUG, "[CLUSTER] sending SUBSCRIBE to node: %s (nrTopics:%d,Mid: %d)", context->id, topic_arr_len, local_mid);
	for(i=0; i<topic_arr_len; i++){
		log__printf(context, MOSQ_LOG_DEBUG, "[CLUSTER]\tSUBSCRIBE topic[%d]: %s", i, topic_arr[i]);
	}

	return packet__queue(context, packet);
}

int send__multi_unsubscribe(struct mosquitto *context, int *mid, char **topic_arr, int topic_arr_len)
{
	int rc, i;
	uint16_t local_mid;
	uint32_t packetlen;
	struct mosquitto__packet *packet;

	if(!context->is_node || !topic_arr || !topic_arr_len)
		return MOSQ_ERR_INVAL;

	packet = mosquitto__calloc(1, sizeof(struct mosquitto__packet));
	if(!packet) return MOSQ_ERR_NOMEM;

	packetlen = 2;

	for(i=0; i<topic_arr_len; i++){
		packetlen += (2+strlen(topic_arr[i]));
	}

	packet->command = UNSUBSCRIBE | (1<<1);
	packet->remaining_length = packetlen;
	rc = packet__alloc(packet);
	if(rc){
		mosquitto__free(packet);
		return rc;
	}

	/* Variable header */
	local_mid = mosquitto__mid_generate(context);
	if(mid) *mid = (int)local_mid;
	packet__write_uint16(packet, local_mid);

	/* Payload */
	for(i=0; i<topic_arr_len; i++)
		packet__write_string(packet, topic_arr[i], strlen(topic_arr[i]));

	log__printf(context, MOSQ_LOG_DEBUG, "[CLUSTER] sending MULTI UNSUBSCRIBE to node: %s (nrTopics:%d,Mid: %d)", context->id, topic_arr_len, local_mid);
	for(i=0; i<topic_arr_len; i++){
		log__printf(context, MOSQ_LOG_DEBUG, "[CLUSTER]\tMULTI UNSUBSCRIBE topic[%d]: %s", i, topic_arr[i]);
	}

	return packet__queue(context, packet);
}

#endif

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

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "config.h"

#include "mosquitto_broker.h"
#include "mosquitto_broker_internal.h"
#include "mosquitto_internal.h"
#include "memory_mosq.h"
#include "send_mosq.h"
#include "packet_mosq.h"

#ifdef WITH_CLUSTER
int handle__private(struct mosquitto_db *db, struct mosquitto *context)
{
	assert(context);
	switch((context->in_packet.command)&0x0F){
		case PRIVATE_SUBSCRIBE:
			return handle__private_subscribe(db, context);
		case PRIVATE_RETAIN:
			return handle__private_retain(db, context);
		case SESSION_REQ:
			return handle__session_req(db, context);
		case SESSION_RESP:
			return handle__session_resp(db, context);
		default:
			return MOSQ_ERR_PROTOCOL;
	}
}

int handle__private_subscribe(struct mosquitto_db *db, struct mosquitto *context)
{
	int rc = 0, rc2;
	uint8_t qos;
	uint16_t mid;
	char *sub;
	char *client_id;
	int slen;

	if(!(context && context->is_peer))
		return MOSQ_ERR_PROTOCOL;

	/* 0. mid */
	if(packet__read_uint16(&context->in_packet, &mid))
		return 1;

	while(context->in_packet.pos < context->in_packet.remaining_length) {
		sub = NULL;
		if(packet__read_string(&context->in_packet, &sub, &slen)){/* 1. topic */
			return 1;
		}
		if(sub){
			if(packet__read_byte(&context->in_packet, &qos)){/* 2. QoS */
				mosquitto__free(sub);
				return 1;
			}

			if(packet__read_string(&context->in_packet, &client_id, &slen)){/* 3. original client id */
				mosquitto__free(sub);
				return 1;
			}

			if(context->last_sub_client_id)
				mosquitto__free(context->last_sub_client_id);
			context->last_sub_client_id = client_id;

			if(packet__read_uint16(&context->in_packet, &context->last_sub_id)){/* 4. sub id */
				mosquitto__free(sub);
				return 1;
			}

			log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Received PRIVATE SUBSCRIBE from peer: %s, topic: %s, client_id: %s, sub_id: %d", 
												context->id,
												sub,
												client_id,
												context->last_sub_id);
			rc2 = sub__add(db, context, sub, qos, &db->subs);
			if(rc2 == MOSQ_ERR_SUCCESS){
				if(sub__retain_queue(db, context, sub, qos)) rc = 1;
			}else if(rc2 != -1){
				rc = rc2;
			}
			mosquitto__free(sub);
		}
	}
	return rc;
}

int handle__private_retain(struct mosquitto_db *db, struct mosquitto *context)
{
	char *topic, *client_id, *rcv_time_hexstr;
	uint8_t qos;
	uint16_t mid = 0, sub_id;
	uint32_t payloadlen;
	int64_t orig_rcv_time;
	int slen;

	mosquitto__payload_uhpa payload;
	struct mosquitto_msg_store *stored;
	struct mosquitto_client_msg *cr_msg = NULL, *tmp_cr_msg = NULL, *prev_cr_msg = NULL;
	struct mosquitto_client_retain *cr = NULL;
	struct mosquitto *client = NULL;

	if(!(context && context->is_node))
		return MOSQ_ERR_PROTOCOL;

	if(packet__read_string(&context->in_packet, &topic, &slen))/* 1. topic */
		return 1;

	if(packet__read_byte(&context->in_packet, &qos)){/* 2. QoS */
		mosquitto__free(topic);
		return 1;
	}

	if(qos >0){
		if(packet__read_uint16(&context->in_packet, &mid)){/* 3. mid */
			mosquitto__free(topic);
			return 1;
		}
	}

	if(packet__read_string(&context->in_packet, &client_id, &slen)){/* 4. client_id */
		mosquitto__free(topic);
		return 1;
	}

	HASH_FIND(hh_id, db->contexts_by_id, client_id, strlen(client_id), client);
	if(!client){
		mosquitto__free(topic);
		return 1;
	}

	if(packet__read_uint16(&context->in_packet, &sub_id)){/* 5. sub id */
		mosquitto__free(topic);
		return 1;
	}

	if(packet__read_string(&context->in_packet, &rcv_time_hexstr, &slen)){/* 6. retain msg recv time */
		mosquitto__free(topic);
		mosquitto__free(client_id);
		return 1;
	}

	mosq_hexstr_to_time(&orig_rcv_time, rcv_time_hexstr);
	mosquitto__free(rcv_time_hexstr);

	if(db->cluster_retain_delay > 0){
		cr = db->retain_list;
		while(cr){
			log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] finding cr..sub_id:%d, expect_send_time:%ld, now:%ld", cr->sub_id, cr->expect_send_time, mosquitto_time());
			if(cr->sub_id == sub_id)
				break;
			cr = cr->next;
		}

		if(cr){
			cr_msg = cr->retain_msgs;
		}else{
			log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Receive a exceed timeout private retain from node:%s, discard.", context->id);
			mosquitto__free(topic);
			mosquitto__free(client_id);
			return MOSQ_ERR_SUCCESS;
		}

		tmp_cr_msg = NULL;
		while(cr_msg){/* find all same topic retain msg, remove the stale ones. */
			if(!strcmp(cr_msg->store->topic, topic)){
				if((int64_t)cr_msg->timestamp >= orig_rcv_time){
					mosquitto__free(topic);
					mosquitto__free(client_id);
					return MOSQ_ERR_SUCCESS;
				}else{
					if(cr_msg == cr->retain_msgs){
						cr->retain_msgs = cr->retain_msgs->next;
					}else if(cr_msg == cr->retain_msgs->next){
						prev_cr_msg = cr->retain_msgs;
					}
					tmp_cr_msg = cr_msg;
					cr_msg = cr_msg->next;
					if(prev_cr_msg)
						prev_cr_msg->next = cr_msg;
					mosquitto__free(tmp_cr_msg);
					db__msg_store_deref(db, &tmp_cr_msg->store);
				}
			}else{
				if(cr_msg == cr->retain_msgs->next){
					prev_cr_msg = cr->retain_msgs;
				}
				cr_msg = cr_msg->next;
				if(prev_cr_msg)
					prev_cr_msg = prev_cr_msg->next;
			}
		}
	}

	payloadlen = context->in_packet.remaining_length - context->in_packet.pos;

	if(payloadlen){
		if(db->config->message_size_limit && payloadlen > db->config->message_size_limit){
			log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Dropped too large PUBLISH from peer: %s (q%d, r%d, m%d, '%s', ... (%ld bytes))", context->id, qos, true, mid, topic, (long)payloadlen);
			mosquitto__free(topic);
			mosquitto__free(client_id);
			return 1;
		}
		if(UHPA_ALLOC(payload, payloadlen+1) == 0){
			mosquitto__free(topic);
			mosquitto__free(client_id);
			return 1;
		}
		if(packet__read_bytes(&context->in_packet, UHPA_ACCESS(payload, payloadlen), payloadlen)){/* 7. payload */
			mosquitto__free(topic);
			mosquitto__free(client_id);
			UHPA_FREE(payload, payloadlen);
			return 1;
		}
	}else{
		mosquitto__free(topic);
		mosquitto__free(client_id);
		return 1;
	}

	log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Receive private retain from node %s (q%d, m%d, '%s', subid %d, client %s ... (%ld bytes))",context->id, qos, mid, topic, sub_id, client_id, (long)payloadlen);

	db__message_store(db, context->id, mid, topic, qos, payloadlen, &payload, true, &stored, 0);
	stored->rcv_time = (time_t)orig_rcv_time;
	stored->from_node = true;
	sub__messages_queue(db, NULL, topic, qos, true, &stored);
	return db__message_insert_into_retain_queue(db, client, mid, mosq_md_out, qos, true, stored, sub_id);
}

int handle__session_req(struct mosquitto_db *db, struct mosquitto *context)
{
	int rc = 0, slen;
	uint8_t clean_session;
	char *client_id;
	struct mosquitto *client_context;

	if(!context->is_peer)
		return 1;

	if(packet__read_string(&context->in_packet, &client_id, &slen))
		return 1;

	if(packet__read_byte(&context->in_packet, &clean_session))
		return 1;

	HASH_FIND(hh_id,db->contexts_by_id, client_id, strlen(client_id), client_context);
	if(!client_context){
		log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Receive SESSION REQ from peer: %s, client_id:%s not found in local db.", context->id, client_id);
		mosquitto__free(client_id);

		return MOSQ_ERR_SUCCESS;
	}
	log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Receive SESSION REQ from peer: %s, client_id:%s has found in local db", context->id, client_id);
	if(!clean_session)
		rc = send__session_resp(context, client_id, client_context);

	log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Client %s has been connected to remote peer, closing old connection.", client_id);
	mosquitto__free(client_id);
	client_context->clean_session = true;
	client_context->save_subs = false;
	client_context->state = mosq_cs_disconnecting;
	do_disconnect(db, client_context);

	return rc;
}

int handle__session_resp(struct mosquitto_db *db, struct mosquitto *context)
{
	char *client_id, *sub_topic, *pub_topic;
	uint8_t sub_qos, pub_qos, pub_flag, pub_dup;
	uint16_t last_mid, nrsubs, nrpubs, pub_mid;
	uint32_t pub_payload_len;
	int i = 0, j, slen;
	bool is_client_dup_sub = false;
	mosquitto__payload_uhpa pub_payload;
	enum mosquitto_msg_direction pub_dir;
	enum mosquitto_msg_state pub_state;
	struct mosquitto *client_context, *node;
	struct mosquitto_msg_store *stored = NULL;
	struct mosquitto_client_retain *cr, *tmpcr;
	struct sub_table *sub_tbl = NULL;

	if(!context->is_node)
		return 1;

	if(packet__read_string(&context->in_packet, &client_id, &slen))
		return 1;

	HASH_FIND(hh_id,db->contexts_by_id, client_id, strlen(client_id), client_context);
	if(!client_context){
		mosquitto__free(client_id);
		return 1;
	}
	
	log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Receive SESSION RESP from node: %s for client: %s", context->id, client_id);

	if(packet__read_uint16(&context->in_packet, &last_mid))
		return 1;
	
	client_context->last_mid = last_mid;

	if(packet__read_uint16(&context->in_packet, &nrsubs))
		return 1;

	if(nrsubs > 0){
		i = client_context->client_sub_count;
		client_context->client_sub_count += nrsubs;
		client_context->client_subs = mosquitto__realloc(client_context->client_subs, client_context->client_sub_count * (sizeof(struct client_subscription_table*)));
	}

	/* handle subs */
	for(; i < client_context->client_sub_count; i ++){
		if(packet__read_string(&context->in_packet, &sub_topic, &slen))
			return 1;

		if(packet__read_byte(&context->in_packet, &sub_qos)){
			mosquitto__free(sub_topic);
			return 1;
		}

		log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER]\tSESSION SUBSCRIBE(%d:%d): topic:%s qos:%d", nrsubs, i, sub_topic, sub_qos);

		if(!IS_SYS_TOPIC(sub_topic)){
			HASH_FIND(hh, db->db_subs, sub_topic, strlen(sub_topic), sub_tbl);
			if(sub_tbl){
				is_client_dup_sub = false;
				for(j=0; j<client_context->client_sub_count - nrsubs; j++){
					if(client_context->client_subs[j] && !strcmp(client_context->client_subs[j]->sub_tbl->topic, sub_topic))
						is_client_dup_sub = true;
				}
				if(!is_client_dup_sub)
					sub_tbl->ref_cnt++;
				else
					continue; /* this is a duplicate client subscription */
				client_context->is_db_dup_sub = true;
			}else{
				client_context->is_db_dup_sub = false;
				sub_tbl = mosquitto__malloc(sizeof(struct sub_table));
				sub_tbl->topic = mosquitto__strdup(sub_topic);
				sub_tbl->ref_cnt = 1;
				HASH_ADD_KEYPTR(hh, db->db_subs, sub_tbl->topic, strlen(sub_tbl->topic), sub_tbl);
			}
			client_context->client_subs[i] = mosquitto__malloc(sizeof(struct client_sub_table));
			client_context->client_subs[i]->sub_qos = sub_qos; /* move remote client sub session to local client */
			client_context->client_subs[i]->sub_tbl = sub_tbl;
			if(!client_context->is_db_dup_sub){
				cr = mosquitto__calloc(1, sizeof(struct mosquitto_client_retain));
				cr->client = client_context;
				cr->next = NULL;
				cr->retain_msgs = NULL;
				cr->expect_send_time = mosquitto_time() + db->cluster_retain_delay;
				cr->sub_id = ++db->sub_id;
				cr->qos = sub_qos;
				client_context->last_sub_id = cr->sub_id;
				/* add a client retain msg list into db */
				if(!db->retain_list){
					db->retain_list = cr;
				}else{
					tmpcr = db->retain_list;
					while(tmpcr->next){
						tmpcr = tmpcr->next;
					}
					tmpcr->next = cr;
				}
			} /* retained flag would be propagete from remote node */
			for(j=0; j<db->node_context_count && !client_context->is_db_dup_sub; j++){/* if this is a dupilicate db sub, then local must have the newest retained msg. */
				node = db->node_contexts[j];
				if(node && node->state == mosq_cs_connected){
					send__private_subscribe(node, NULL, sub_topic, sub_qos, client_context->id, db->sub_id);
				}
			}
		}
		sub__add(db, client_context, sub_topic, sub_qos, &db->subs);
		sub__retain_queue(db, client_context, sub_topic, sub_qos);
		mosquitto__free(sub_topic);
	}

	if(packet__read_uint16(&context->in_packet, &nrpubs)) return 1;
	/* handle pubs */
	for(i = 0; i < nrpubs; i ++){
		if(packet__read_string(&context->in_packet, &pub_topic, &slen)) return 1;
		if(packet__read_byte(&context->in_packet, &pub_flag)) return 1;
		pub_qos = pub_flag & 0x03;
		pub_dup = (pub_flag & 0x04) >> 2;
		pub_dir = (enum mosquitto_msg_direction)((pub_flag & 0x08) >> 3);
		pub_state = (enum mosquitto_msg_state)((pub_flag & 0xF0) >> 4);

		if(packet__read_uint16(&context->in_packet, &pub_mid)){
			mosquitto__free(pub_topic);
			return 1;
		}
		if(packet__read_uint32(&context->in_packet, &pub_payload_len)){
			mosquitto__free(pub_topic);
			return 1;
		}
		pub_payload.ptr = NULL;
		if(UHPA_ALLOC(pub_payload, pub_payload_len+1) == 0){
			mosquitto__free(pub_topic);
			return 1;
		}
		if(packet__read_bytes(&context->in_packet, UHPA_ACCESS(pub_payload, pub_payload_len), pub_payload_len)){
			mosquitto__free(pub_topic);
			return 1;
		}
		log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER]\tSESSION PUBLISH(%d:%d) (d%d, q%d, s%d, m%d, '%s', ... (%ld bytes))", nrpubs,i, pub_dup, pub_qos, pub_state, pub_mid, pub_topic, (long)pub_payload_len);
		if(db__message_store(db, context->id, pub_mid, pub_topic, pub_qos, pub_payload_len, &pub_payload, 0, &stored, 0)){
			UHPA_FREE(pub_payload, pub_payload_len);
			return 1;
		}
		if(db__message_session_pub_insert(db, client_context, pub_mid, pub_state, pub_dir, pub_dup, pub_qos, false, stored) == MOSQ_ERR_NOMEM){
			mosquitto__free(pub_topic);
			UHPA_FREE(pub_payload, pub_payload_len);
			return 1;
		}
	}
	db__message_reconnect_reset(db, client_context);

	return MOSQ_ERR_SUCCESS;
}
#endif

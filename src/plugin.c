/*
Copyright (c) 2016-2018 Roger Light <roger@atchoo.org>

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

#include "config.h"

#include "mosquitto_broker_internal.h"
#include "mosquitto_internal.h"
#include "mosquitto_broker.h"

#ifdef WITH_TLS
#  include <openssl/ssl.h>
#endif

const char *mosquitto_client_address(const struct mosquitto *client)
{
	return client->address;
}


bool mosquitto_client_clean_session(const struct mosquitto *client)
{
	return client->clean_session;
}


const char *mosquitto_client_id(const struct mosquitto *client)
{
	return client->id;
}


int mosquitto_client_keepalive(const struct mosquitto *client)
{
	return client->keepalive;
}


void *mosquitto_client_certificate(const struct mosquitto *client)
{
#ifdef WITH_TLS
	if(client->ssl){
		return SSL_get_peer_certificate(client->ssl);
	}else{
		return NULL;
	}
#else
	return NULL;
#endif
}


int mosquitto_client_protocol(const struct mosquitto *client)
{
	return client->protocol;
}


int mosquitto_client_sub_count(const struct mosquitto *client)
{
	return client->sub_count;
}


const char *mosquitto_client_username(const struct mosquitto *context)
{
#ifdef WITH_BRIDGE
	if(context->bridge){
		return context->bridge->local_username;
	}else
#endif
	{
		return context->username;
	}
}

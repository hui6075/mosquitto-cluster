/*
Copyright (c) 2010-2012 Roger Light <roger@atchoo.org>

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

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <mosquitto_broker_internal.h>
#include <memory_mosq.h>
#include <persist.h>

#define mosquitto__malloc(A) malloc((A))
#define mosquitto__free(A) free((A))
#define _mosquitto_malloc(A) malloc((A))
#define _mosquitto_free(A) free((A))
#include <uthash.h>


struct client_chunk
{
	UT_hash_handle hh_id;
	char *id;
	int subscriptions;
	int subscription_size;
	int messages;
	long message_size;
};

struct msg_store_chunk
{
	UT_hash_handle hh;
	dbid_t store_id;
	uint32_t length;
};

struct db_sub
{
	char *client_id;
	char *topic;
	uint8_t qos;
};

struct db_client
{
	char *client_id;
	uint16_t last_mid;
	time_t disconnect_t;
};

struct db_client_msg
{
	char *client_id;
	uint8_t qos, retain, direction, state, dup;
	dbid_t store_id;
	uint16_t mid;
};

struct db_msg
{
	dbid_t store_id;
	uint32_t payloadlen;
	uint16_t source_mid, mid;
	uint8_t qos, retain;
	uint8_t *payload;
	char *source_id;
	char *topic;
};

static uint32_t db_version;
static int stats = 0;
static int client_stats = 0;
static int do_print = 1;

struct client_chunk *clients_by_id = NULL;
struct msg_store_chunk *msgs_by_id = NULL;

static void
free__db_sub(struct db_sub *sub)
{
	if (sub->client_id) {
		free(sub->client_id);
	}
	if (sub->topic) {
		free(sub->topic);
	}
}

static void
free__db_client(struct db_client *client)
{
	if (client->client_id) {
		free(client->client_id);
	}
}

static void
free__db_client_msg(struct db_client_msg *msg)
{
	if (msg->client_id) {
		free(msg->client_id);
	}
}

static void
free__db_msg(struct db_msg *msg)
{
	if (msg->source_id) {
		free(msg->source_id);
	}
	if (msg->topic) {
		free(msg->topic);
	}
	if (msg->payload) {
		free(msg->payload);
	}
}

static void
print_db_client(struct db_client *client, int length)
{
	printf("DB_CHUNK_CLIENT:\n");
	printf("\tLength: %d\n", length);
	printf("\tClient ID: %s\n", client->client_id);
	printf("\tLast MID: %d\n", client->last_mid);
	printf("\tDisconnect time: %ld\n", client->disconnect_t);
}

static void
print_db_client_msg(struct db_client_msg *msg, int length)
{
	printf("DB_CHUNK_CLIENT_MSG:\n");
	printf("\tLength: %d\n", length);
	printf("\tClient ID: %s\n", msg->client_id);
	printf("\tStore ID: %" PRIu64 "\n", msg->store_id);
	printf("\tMID: %d\n", msg->mid);
	printf("\tQoS: %d\n", msg->qos);
	printf("\tRetain: %d\n", msg->retain);
	printf("\tDirection: %d\n", msg->direction);
	printf("\tState: %d\n", msg->state);
	printf("\tDup: %d\n", msg->dup);
}

static void
print_db_sub(struct db_sub *sub, int length)
{
	printf("DB_CHUNK_SUB:\n");
	printf("\tLength: %d\n", length);
	printf("\tClient ID: %s\n", sub->client_id);
	printf("\tTopic: %s\n", sub->topic);
	printf("\tQoS: %d\n", sub->qos);
}

static void
print_db_msg(struct db_msg *msg, int length)
{
	printf("DB_CHUNK_MSG_STORE:\n");
	printf("\tLength: %d\n", length);
	printf("\tStore ID: %" PRIu64 "\n", msg->store_id);
	printf("\tSource ID: %s\n", msg->source_id);
	printf("\tSource MID: %d\n", msg->source_mid);
	printf("\tMID: %d\n", msg->mid);
	printf("\tTopic: %s\n", msg->topic);
	printf("\tQoS: %d\n", msg->qos);
	printf("\tRetain: %d\n", msg->retain);
	printf("\tPayload Length: %d\n", msg->payloadlen);

	bool binary = false;
	for(int i=0; i<msg->payloadlen; i++){
		if(msg->payload[i] == 0) binary = true;
	}
	if(binary == false && msg->payloadlen<256){
		printf("\tPayload: %s\n", msg->payload);
	}
}


static int db__client_chunk_restore(struct mosquitto_db *db, FILE *db_fd, struct db_client *client)
{
	uint16_t i16temp, slen;
	int rc = 0;
	struct client_chunk *cc;

	read_e(db_fd, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	if(!slen){
		fprintf(stderr, "Error: Corrupt persistent database.");
		fclose(db_fd);
		return 1;
	}
	client->client_id = calloc(slen+1, sizeof(char));
	if(!client->client_id){
		fclose(db_fd);
		fprintf(stderr, "Error: Out of memory.");
		return 1;
	}
	read_e(db_fd, client->client_id, slen);

	read_e(db_fd, &i16temp, sizeof(uint16_t));
	client->last_mid = ntohs(i16temp);

	if(db_version == 2){
		client->disconnect_t = time(NULL);
	}else{
		read_e(db_fd, &client->disconnect_t, sizeof(time_t));
	}

	if(client_stats){
		cc = calloc(1, sizeof(struct client_chunk));
		if(!cc){
			errno = ENOMEM;
			goto error;
		}
		cc->id = strdup(client->client_id);
		HASH_ADD_KEYPTR(hh_id, clients_by_id, cc->id, strlen(cc->id), cc);
	}

	return rc;
error:
	fprintf(stderr, "Error: %s.", strerror(errno));
	if(db_fd) fclose(db_fd);
	free(client->client_id);
	return 1;
}

static int db__client_msg_chunk_restore(struct mosquitto_db *db, FILE *db_fd, uint32_t length, struct db_client_msg *msg)
{
	dbid_t i64temp;
	uint16_t i16temp, slen;
	struct client_chunk *cc;
	struct msg_store_chunk *msc;

	read_e(db_fd, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	if(!slen){
		fprintf(stderr, "Error: Corrupt persistent database.");
		fclose(db_fd);
		return 1;
	}
	msg->client_id = calloc(slen+1, sizeof(char));
	if(!msg->client_id){
		fclose(db_fd);
		fprintf(stderr, "Error: Out of memory.");
		return 1;
	}
	read_e(db_fd, msg->client_id, slen);

	read_e(db_fd, &i64temp, sizeof(dbid_t));
	msg->store_id = i64temp;

	read_e(db_fd, &i16temp, sizeof(uint16_t));
	msg->mid = ntohs(i16temp);

	read_e(db_fd, &msg->qos, sizeof(uint8_t));
	read_e(db_fd, &msg->retain, sizeof(uint8_t));
	read_e(db_fd, &msg->direction, sizeof(uint8_t));
	read_e(db_fd, &msg->state, sizeof(uint8_t));
	read_e(db_fd, &msg->dup, sizeof(uint8_t));

	if(client_stats){
		HASH_FIND(hh_id, clients_by_id, msg->client_id, strlen(msg->client_id), cc);
		if(cc){
			cc->messages++;
			cc->message_size += length;

			HASH_FIND(hh, msgs_by_id, &msg->store_id, sizeof(dbid_t), msc);
			if(msc){
				cc->message_size += msc->length;
			}
		}
	}

	return 0;
error:
	fprintf(stderr, "Error: %s.", strerror(errno));
	if(db_fd) fclose(db_fd);
	free(msg->client_id);
	return 1;
}

static int db__msg_store_chunk_restore(struct mosquitto_db *db, FILE *db_fd, uint32_t length, struct db_msg *msg)
{
	dbid_t i64temp;
	uint32_t i32temp;
	uint16_t i16temp, slen;
	int rc = 0;
	struct msg_store_chunk *mcs;

	read_e(db_fd, &i64temp, sizeof(dbid_t));
	msg->store_id = i64temp;

	read_e(db_fd, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	if(slen){
		msg->source_id = calloc(slen+1, sizeof(char));
		if(!msg->source_id){
			fclose(db_fd);
			fprintf(stderr, "Error: Out of memory.");
			return 1;
		}
		if(fread(msg->source_id, 1, slen, db_fd) != slen){
			fprintf(stderr, "Error: %s.", strerror(errno));
			fclose(db_fd);
			free(msg->source_id);
			return 1;
		}
	}
	read_e(db_fd, &i16temp, sizeof(uint16_t));
	msg->source_mid = ntohs(i16temp);

	read_e(db_fd, &i16temp, sizeof(uint16_t));
	msg->mid = ntohs(i16temp);

	read_e(db_fd, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	if(slen){
		msg->topic = calloc(slen+1, sizeof(char));
		if(!msg->topic){
			fclose(db_fd);
			free(msg->source_id);
			fprintf(stderr, "Error: Out of memory.");
			return 1;
		}
		if(fread(msg->topic, 1, slen, db_fd) != slen){
			fprintf(stderr, "Error: %s.", strerror(errno));
			fclose(db_fd);
			free(msg->source_id);
			free(msg->topic);
			return 1;
		}
	}else{
		fprintf(stderr, "Error: Invalid msg_store chunk when restoring persistent database.");
		fclose(db_fd);
		free(msg->source_id);
		return 1;
	}
	read_e(db_fd, &msg->qos, sizeof(uint8_t));
	read_e(db_fd, &msg->retain, sizeof(uint8_t));
	
	read_e(db_fd, &i32temp, sizeof(uint32_t));
	msg->payloadlen = ntohl(i32temp);

	if(msg->payloadlen){
		msg->payload = malloc(msg->payloadlen+1);
		if(!msg->payload){
			fclose(db_fd);
			free(msg->source_id);
			free(msg->topic);
			fprintf(stderr, "Error: Out of memory.");
			return 1;
		}
		memset(msg->payload, 0, msg->payloadlen+1);
		if(fread(msg->payload, 1, msg->payloadlen, db_fd) != msg->payloadlen){
			fprintf(stderr, "Error: %s.", strerror(errno));
			fclose(db_fd);
			free(msg->source_id);
			free(msg->topic);
			free(msg->payload);
			return 1;
		}
	}

	if(client_stats){
		mcs = calloc(1, sizeof(struct msg_store_chunk));
		if(!mcs){
			errno = ENOMEM;
			goto error;
		}
		mcs->store_id = msg->store_id;
		mcs->length = length;
		HASH_ADD(hh, msgs_by_id, store_id, sizeof(dbid_t), mcs);
	}

	return rc;
error:
	fprintf(stderr, "Error: %s.", strerror(errno));
	if(db_fd) fclose(db_fd);
	free(msg->source_id);
	free(msg->topic);
	return 1;
}

static int db__retain_chunk_restore(struct mosquitto_db *db, FILE *db_fd)
{
	dbid_t i64temp, store_id;

	if(fread(&i64temp, sizeof(dbid_t), 1, db_fd) != 1){
		fprintf(stderr, "Error: %s.", strerror(errno));
		fclose(db_fd);
		return 1;
	}
	store_id = i64temp;
	if(do_print) printf("\tStore ID: %" PRIu64 "\n", store_id);
	return 0;
}

static int db__sub_chunk_restore(struct mosquitto_db *db, FILE *db_fd, uint32_t length, struct db_sub *sub)
{
	uint16_t i16temp, slen;
	int rc = 0;
	struct client_chunk *cc;

	read_e(db_fd, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	sub->client_id = calloc(slen+1, sizeof(char));
	if(!sub->client_id){
		fclose(db_fd);
		fprintf(stderr, "Error: Out of memory.");
		return 1;
	}
	read_e(db_fd, sub->client_id, slen);
	read_e(db_fd, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	sub->topic = calloc(slen+1, sizeof(char));
	if(!sub->topic){
		fclose(db_fd);
		fprintf(stderr, "Error: Out of memory.");
		free(sub->client_id);
		return 1;
	}
	read_e(db_fd, sub->topic, slen);
	read_e(db_fd, &sub->qos, sizeof(uint8_t));

	if(client_stats){
		HASH_FIND(hh_id, clients_by_id, sub->client_id, strlen(sub->client_id), cc);
		if(cc){
			cc->subscriptions++;
			cc->subscription_size += length;
		}
	}

	return rc;
error:
	fprintf(stderr, "Error: %s.", strerror(errno));
	if(db_fd >= 0) fclose(db_fd);
	return 1;
}

int main(int argc, char *argv[])
{
	FILE *fd;
	char header[15];
	int rc = 0;
	uint32_t crc;
	dbid_t i64temp;
	uint32_t i32temp, length;
	uint16_t i16temp, chunk;
	uint8_t i8temp;
	ssize_t rlen;
	struct mosquitto_db db;
	char *filename;
	long cfg_count = 0;
	long msg_store_count = 0;
	long client_msg_count = 0;
	long retain_count = 0;
	long sub_count = 0;
	long client_count = 0;
	struct client_chunk *cc, *cc_tmp;

	if(argc == 2){
		filename = argv[1];
	}else if(argc == 3 && !strcmp(argv[1], "--stats")){
		stats = 1;
		do_print = 0;
		filename = argv[2];
	}else if(argc == 3 && !strcmp(argv[1], "--client-stats")){
		client_stats = 1;
		do_print = 0;
		filename = argv[2];
	}else{
		fprintf(stderr, "Usage: db_dump [--stats | --client-stats] <mosquitto db filename>\n");
		return 1;
	}
	memset(&db, 0, sizeof(struct mosquitto_db));
	fd = fopen(filename, "rb");
	if(!fd) return 0;
	read_e(fd, &header, 15);
	if(!memcmp(header, magic, 15)){
		if(do_print) printf("Mosquitto DB dump\n");
		// Restore DB as normal
		read_e(fd, &crc, sizeof(uint32_t));
		if(do_print) printf("CRC: %d\n", crc);
		read_e(fd, &i32temp, sizeof(uint32_t));
		db_version = ntohl(i32temp);
		if(do_print) printf("DB version: %d\n", db_version);

		while(rlen = fread(&i16temp, sizeof(uint16_t), 1, fd), rlen == 1){
			chunk = ntohs(i16temp);
			read_e(fd, &i32temp, sizeof(uint32_t));
			length = ntohl(i32temp);
			switch(chunk){
				case DB_CHUNK_CFG:
					cfg_count++;
					if(do_print) printf("DB_CHUNK_CFG:\n");
					if(do_print) printf("\tLength: %d\n", length);
					read_e(fd, &i8temp, sizeof(uint8_t)); // shutdown
					if(do_print) printf("\tShutdown: %d\n", i8temp);
					read_e(fd, &i8temp, sizeof(uint8_t)); // sizeof(dbid_t)
					if(do_print) printf("\tDB ID size: %d\n", i8temp);
					if(i8temp != sizeof(dbid_t)){
						fprintf(stderr, "Error: Incompatible database configuration (dbid size is %d bytes, expected %ld)",
								i8temp, sizeof(dbid_t));
						fclose(fd);
						return 1;
					}
					read_e(fd, &i64temp, sizeof(dbid_t));
					if(do_print) printf("\tLast DB ID: %ld\n", (long)i64temp);
					break;

				case DB_CHUNK_MSG_STORE:
					msg_store_count++;
					struct db_msg msg = {0};
					if(db__msg_store_chunk_restore(&db, fd, length, &msg)) return 1;
					if(do_print) {
						print_db_msg(&msg, length);
					}
					free__db_msg(&msg);
					break;

				case DB_CHUNK_CLIENT_MSG:
					client_msg_count++;
					struct db_client_msg cmsg = {0};
					if(db__client_msg_chunk_restore(&db, fd, length, &cmsg)) return 1;
					if(do_print) {
						print_db_client_msg(&cmsg, length);
					}
					free__db_client_msg(&cmsg);
					break;

				case DB_CHUNK_RETAIN:
					retain_count++;
					if(do_print) printf("DB_CHUNK_RETAIN:\n");
					if(do_print) printf("\tLength: %d\n", length);
					if(db__retain_chunk_restore(&db, fd)) return 1;
					break;

				case DB_CHUNK_SUB:
					sub_count++;
					struct db_sub sub = {0};
					if(db__sub_chunk_restore(&db, fd, length, &sub)) return 1;
					if(do_print) {
						print_db_sub(&sub, length);
					}
					free__db_sub(&sub);
					break;

				case DB_CHUNK_CLIENT:
					client_count++;
					struct db_client client = {0};
					if(db__client_chunk_restore(&db, fd, &client)) return 1;
					if(do_print) {
						print_db_client(&client, length);
					}
					free__db_client(&client);
					break;

				default:
					fprintf(stderr, "Warning: Unsupported chunk \"%d\" in persistent database file. Ignoring.\n", chunk);
					fseek(fd, length, SEEK_CUR);
					break;
			}
		}
		if(rlen < 0) goto error;
	}else{
		fprintf(stderr, "Error: Unrecognised file format.");
		rc = 1;
	}

	fclose(fd);

	if(stats){
		printf("DB_CHUNK_CFG:        %ld\n", cfg_count);
		printf("DB_CHUNK_MSG_STORE:  %ld\n", msg_store_count);
		printf("DB_CHUNK_CLIENT_MSG: %ld\n", client_msg_count);
		printf("DB_CHUNK_RETAIN:     %ld\n", retain_count);
		printf("DB_CHUNK_SUB:        %ld\n", sub_count);
		printf("DB_CHUNK_CLIENT:     %ld\n", client_count);
	}

	if(client_stats){
		HASH_ITER(hh_id, clients_by_id, cc, cc_tmp){
			printf("SC: %d SS: %d MC: %d MS: %ld   ", cc->subscriptions, cc->subscription_size, cc->messages, cc->message_size);
			printf("%s\n", cc->id);
			free(cc->id);
		}
	}

	return rc;
error:
	fprintf(stderr, "Error: %s.", strerror(errno));
	if(fd >= 0) fclose(fd);
	return 1;
}


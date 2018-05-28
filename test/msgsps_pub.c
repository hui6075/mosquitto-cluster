/* This provides a crude manner of testing the performance of a broker in messages/s. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <mosquitto.h>

#include <msgsps_common.h>

static bool run = true;
static int message_count = 0;
static struct timeval start, stop;

void my_connect_callback(struct mosquitto *mosq, void *obj, int rc)
{
	printf("rc: %d\n", rc);
	gettimeofday(&start, NULL);
}

void my_disconnect_callback(struct mosquitto *mosq, void *obj, int result)
{
	run = false;
}

void my_publish_callback(struct mosquitto *mosq, void *obj, int mid)
{
	message_count++;
	if(message_count == MESSAGE_COUNT){
		gettimeofday(&stop, NULL);
		mosquitto_disconnect((struct mosquitto *)obj);
	}
}

int main(int argc, char *argv[])
{
	struct mosquitto *mosq;
	int i;
	double dstart, dstop, diff;
	uint8_t *buf;
	
	buf = malloc(MESSAGE_SIZE*MESSAGE_COUNT);
	if(!buf){
		printf("Error: Out of memory.\n");
		return 1;
	}

	start.tv_sec = 0;
	start.tv_usec = 0;
	stop.tv_sec = 0;
	stop.tv_usec = 0;

	mosquitto_lib_init();

	mosq = mosquitto_new("perftest", true, NULL);
	mosquitto_connect_callback_set(mosq, my_connect_callback);
	mosquitto_disconnect_callback_set(mosq, my_disconnect_callback);
	mosquitto_publish_callback_set(mosq, my_publish_callback);

	mosquitto_connect(mosq, HOST, PORT, 600);

	mosquitto_loop_start(mosq);

	i=0;
	for(i=0; i<MESSAGE_COUNT; i++){
		mosquitto_publish(mosq, NULL, "perf/test", MESSAGE_SIZE, &buf[i*MESSAGE_SIZE], PUB_QOS, false);
	}
	mosquitto_loop_stop(mosq, false);

	dstart = (double)start.tv_sec*1.0e6 + (double)start.tv_usec;
	dstop = (double)stop.tv_sec*1.0e6 + (double)stop.tv_usec;
	diff = (dstop-dstart)/1.0e6;

	printf("Start: %g\nStop: %g\nDiff: %g\nMessages/s: %g\n", dstart, dstop, diff, (double)MESSAGE_COUNT/diff);

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

	return 0;
}

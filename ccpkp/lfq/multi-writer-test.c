#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h> 

#include "lfq.h"

void set_size(char *buf, uint16_t size) {
	*(((uint16_t *)buf)+1) = size;
}

void examine_buf(const char *buf, uint16_t size) {
	int base = 0;
	printf("|");
	for (int i = 0; i < size; i++) {
		printf("%c |", buf[i]);
	}
	printf("\n|");
	for (int i = 0; i < size; i++) {
		printf("%02X|", buf[i]);
	}
	printf("\n");
}

void print_buf(char *buf) {
	buf += 4;
	printf("%s\n", buf);
}

char *create_buf(const char *str, size_t *buf_len) {
	size_t len = strlen(str)+1;
	char *buf = malloc(len + 4);
	memcpy(buf+4, str, len);
	set_size(buf, len+4);
	*buf_len = (len+4);
	return buf;
}


void *reader(void *args) {
	struct pipe *p = (struct pipe *)args;
	char recv[2048];
	int num_recvd = 0;
    usleep(1000);
	while (num_recvd < 10000) {
		int read = dp_read(p, recv, 2048);
		if (read > 0) {
			char *p = recv;
			while (read > 0) {
				int sz = read_portus_msg_size(p);
				p+= sz;
				read -= sz;
				num_recvd++;
			}
		}
		usleep(rand() % 250);
	}
	return NULL;
}

void *writer1(void *args) {
	struct pipe *p = (struct pipe *)args;
	size_t buf_len;

	for (int i=0; i<2500; i++) {
		int wrote = 0;
		while (wrote <= 0) {
			usleep(100);
			char a[25];
			sprintf(a, "i'm writer 1, msg=%2d", i);
			const char *buf = create_buf((const char *)a, &buf_len);
			wrote = ccp_write(p, buf, buf_len, 1);
			free((void*)buf);
		}
	}
	usleep(rand() % 10);
    PDEBUG("[writer 1] done writing\n");
	return NULL;
}
void *writer2(void *args) {
	struct pipe *p = (struct pipe *)args;
	size_t buf_len;
	for (int i=0; i<5000; i++) {
		int wrote = 0;
		while (wrote <= 0) {
			usleep(100);
			char a[25];
			sprintf(a, "i'm writer 2, msg=%2d", i);
			const char *buf = create_buf((const char *)a, &buf_len);
			wrote = ccp_write(p, buf, buf_len, 2);
			free((void*)buf);
		}
		usleep(rand() % 10);
	}
    PDEBUG("[writer 2] done writing\n");
	return NULL;
}
void *writer3(void *args) {
	struct pipe *p = (struct pipe *)args;
	size_t buf_len;
	for (int i=0; i<2500; i++) {
		int wrote = 0;
		while (wrote <= 0) {
			usleep(100);
			char a[25];
			sprintf(a, "i'm writer 3, msg=%2d", i);
			const char *buf = create_buf((const char *)a, &buf_len);
			wrote = ccp_write(p, buf, buf_len, 3);
			free((void*)buf);
		}
		usleep(rand() % 10);
	}
    PDEBUG("[writer 3] done writing\n");
	return NULL;
}

int main() {
	srand(time(NULL));

        printf("LFQ multiple writers test\n");

        printf("blocking......");

        {
            struct pipe *p = (struct pipe *) malloc(sizeof(struct pipe));
            init_pipe(p, true);
            pthread_t t1, t2, t3, t4;
            pthread_create(&t1, NULL, reader, (void *)p);
            pthread_create(&t2, NULL, writer1, (void *)p);
            pthread_create(&t3, NULL, writer2, (void *)p);
            pthread_create(&t4, NULL, writer3, (void *)p);
            pthread_join(t1, NULL);
            pthread_join(t2, NULL);
            pthread_join(t3, NULL);
            pthread_join(t4, NULL);
            free_pipe(p);
        }

        printf("passed\n");

        printf("nonblocking...");

        {
            struct pipe *p = (struct pipe *) malloc(sizeof(struct pipe));
            init_pipe(p, false);
            pthread_t t1, t2, t3, t4;
            pthread_create(&t1, NULL, reader, (void *)p);
            pthread_create(&t2, NULL, writer1, (void *)p);
            pthread_create(&t3, NULL, writer2, (void *)p);
            pthread_create(&t4, NULL, writer3, (void *)p);
            pthread_join(t1, NULL);
            pthread_join(t2, NULL);
            pthread_join(t3, NULL);
            pthread_join(t4, NULL);
            free_pipe(p);
        }

        printf("passed\n");

	return 0;
}

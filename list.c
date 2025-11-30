#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

#include "list.h"

static void generate_random_string(char *buf) {
    // srand(time(NULL));
	int len = (int)rand() % 100;

	for (int i = 0; i < len; i++) {
		buf[i] = 'c';
	}

	buf[len] = '\0';
}

Storage* storage_init(int n) {
	Storage *st;
	Node *prev;
	int err;

	st = malloc(sizeof(Storage));
	if (!st) {
		printf("storage_init: cannot allocate memory for storage\n");
		abort();
	}

	st->first = NULL;
	prev = NULL;

	for (int i = 0; i < n; i++) {
		Node *node = malloc(sizeof(Node));
		if (!node) {
			printf("storage_init: cannot allocate memory for node\n");
			abort();
		}

		generate_random_string(node->value);
		node->next = NULL;

		err = pthread_mutex_init(&node->sync, NULL);
		if (err) {
			printf("storage_init: pthread_mutex_init() failed: %s\n", strerror(err));
			abort();
		}

		if (!st->first) 
			st->first = node;
		else
			prev->next = node;
		
		prev = node;
	}

	return st;
}

void storage_destroy(Storage *st) {
	Node *cur;
	Node *next;

	cur = st->first;
	while (cur) {
		next = cur->next;

		pthread_mutex_destroy(&cur->sync);
		free(cur);

		cur = next;
	}

	free(st);
}

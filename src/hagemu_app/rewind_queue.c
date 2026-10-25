#include "rewind_queue.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

bool rewind_queue_create(struct RewindQueue *queue, size_t item_size, size_t item_count) {
	memset(queue, 0, sizeof(struct RewindQueue));
	fprintf(stderr, "Allocating %zu MiB for the rewind queue\n", item_size * item_count / (1024 * 1024));
	queue->data = malloc(item_size * item_count);
	if (queue->data == NULL) {
		fprintf(stderr, "[ERROR] Failed to allocate memory for the rewind queue\n");
		return false;
	}
	queue->item_size = item_size;
	queue->capacity = item_count;
	return true;
}

void rewind_queue_free(struct RewindQueue *queue) {
	free(queue->data);
	memset(queue, 0, sizeof(struct RewindQueue));
}

void rewind_queue_reset(struct RewindQueue *queue) {
	queue->head = 0;
	queue->length = 0;
}

void rewind_queue_pop(struct RewindQueue *queue, struct HagemuGB *gb) {
	if (queue->capacity == 0 || queue->length == 0)
		return;

	if (queue->head == 0)
		queue->head = queue->capacity - 1;
	else
		queue->head--;
	queue->length--;
	hagemu_snapshot_restore(gb, queue->data + queue->item_size * queue->head);
}

void rewind_queue_push(struct RewindQueue *queue, const struct HagemuGB *gb) {
	if (queue->capacity == 0)
		return;

	hagemu_snapshot_record(gb, queue->data + queue->item_size * queue->head);
	queue->head++;
	queue->head %= queue->capacity;
	if (queue->length < queue->capacity)
		queue->length++;
}

#ifndef HAGEMU_REWIND_QUEUE_H
#define HAGEMU_REWIND_QUEUE_H

#include "hagemu.h"

struct RewindQueue {
	uint8_t *data;
	size_t item_size;
	size_t head;
	size_t length;
	size_t capacity;
};

struct RewindQueue *rewind_queue_create(size_t item_size, size_t item_count);
void rewind_queue_free(struct RewindQueue *queue);
void rewind_queue_reset(struct RewindQueue *queue);
void rewind_queue_pop(struct RewindQueue *queue, struct HagemuGB *gb);
void rewind_queue_push(struct RewindQueue *queue, const struct HagemuGB *gb);

#endif

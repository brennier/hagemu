#ifndef HAGEMU_REWIND_QUEUE_H
#define HAGEMU_REWIND_QUEUE_H

#include "hagemu.h"

#define REWIND_QUEUE_DATA_CAPACITY (8 * 1024 * 1024) // 8 MiB
#define REWIND_QUEUE_INDEX_CAPACITY 7200 // Two minutes worth of indices

struct RewindQueueIndex {
	uint32_t pos;
	uint32_t size;
};

struct RewindQueue {
	uint8_t data[REWIND_QUEUE_DATA_CAPACITY];
	struct RewindQueueIndex index[REWIND_QUEUE_INDEX_CAPACITY];
	uint16_t index_current;
	uint16_t index_length;
	size_t snapshot_size;
	bool snapshot_latest_is_taken;
	uint8_t *snapshot_latest;
};

struct RewindQueue *rewind_queue_create(size_t item_size);
void rewind_queue_free(struct RewindQueue *queue);
void rewind_queue_reset(struct RewindQueue *queue);
void rewind_queue_pop(struct RewindQueue *queue, struct HagemuGB *gb);
void rewind_queue_push(struct RewindQueue *queue, const struct HagemuGB *gb);

#endif

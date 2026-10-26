#include "rewind_queue.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int write_varint(uint8_t *location, int number) {
	int i = 0;
	while (number > 127) {
		uint8_t reminder = number % 128;
		location[i++] = 0x80 | reminder;
		number /= 128;
	}
	location[i++] = (uint8_t)number;
	return i;
}

static int read_varint(uint8_t *location, int *out_number) {
	int i = 0;
	int result = 0;
	while (location[i] & 0x80) {
		result += ((location[i] & 0x7F) << 7 * i);
		i++;
	}
	result += ((location[i] & 0x7F) << 7 * i);
	i++;
	*out_number = result;
	return i;
}

static size_t rewind_queue_compress(uint8_t * restrict output, size_t output_length,
				    const uint8_t * restrict input, size_t input_length) {
	uint32_t count = 0;
	size_t i = 0;
	size_t j = 0;
	bool counting_zeros = true;
	while (i < input_length) {
		if (counting_zeros && input[i] != 0) {
			j += write_varint(output + j, count);
			count = 0;
			counting_zeros = false;
		} else if (!counting_zeros && input[i] == 0 && input[i+1] == 0) {
			j += write_varint(output + j, count);
			memcpy(output + j, input + i - count, count);
			j += count;
			count = 0;
			counting_zeros = true;
		} else {
			count++;
		}
		i++;
	}
	if (counting_zeros) {
		j += write_varint(output + j, count);
	} else {
		j += write_varint(output + j, count);
		memcpy(output + j, input + i - count, count);
	}
	return j;
}

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
	// If there's another snapshot before the one we just popped, XOR it with
	// the one we just popped.
	if (queue->length > 0) {
		size_t previous_head = (queue->head == 0) ? queue->capacity - 1 : queue->head - 1;
		uint8_t *previous_snapshot = queue->data + queue->item_size * previous_head;
		uint8_t *current_snapshot  = queue->data + queue->item_size * queue->head;
		for (size_t i = 0; i < queue->item_size; i++) {
			previous_snapshot[i] ^= current_snapshot[i];
		}
	}
}

void rewind_queue_push(struct RewindQueue *queue, const struct HagemuGB *gb) {
	if (queue->capacity == 0)
		return;

	hagemu_snapshot_record(gb, queue->data + queue->item_size * queue->head);
	// If there's a previous snapshot, XOR it with the new snapshot
	if (queue->length > 0) {
		size_t previous_head = (queue->head == 0) ? queue->capacity - 1 : queue->head - 1;
		uint8_t *previous_snapshot = queue->data + queue->item_size * previous_head;
		uint8_t *current_snapshot  = queue->data + queue->item_size * queue->head;
		for (size_t i = 0; i < queue->item_size; i++) {
			previous_snapshot[i] ^= current_snapshot[i];
		}
	}
	queue->head++;
	queue->head %= queue->capacity;
	if (queue->length < queue->capacity)
		queue->length++;
}

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

static int read_varint(const uint8_t *location, int *out_number) {
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

static size_t run_length_upper_bound(size_t data_size) {
	return data_size + (data_size / 128) + 16;
}

static size_t run_length_encode(uint8_t * restrict output, size_t output_length,
				const uint8_t * restrict input, size_t input_length) {
	uint32_t count = 0;
	size_t i = 0;
	size_t j = 0;
	bool counting_zeros = true;
	while (i < input_length) {
		if (counting_zeros && input[i] != 0) {
			j += write_varint(output + j, count);
			count = 1;
			counting_zeros = false;
		} else if (!counting_zeros && input[i] == 0 &&
			   i + 1 < input_length && input[i+1] == 0) {
			j += write_varint(output + j, count);
			memcpy(output + j, input + i - count, count);
			j += count;
			count = 2;
			i += 1;
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
		j += count;
	}
	return j;
}

static size_t run_length_decode(uint8_t * restrict output, size_t output_length,
				const uint8_t * restrict input, size_t input_length) {
	size_t i = 0;
	size_t j = 0;
	bool expanding_zeros = true;
	memset(output, 0, output_length);
	while (i < input_length) {
		int count;
		i += read_varint(input + i, &count);
		if (expanding_zeros) {
			j += count;
		} else {
			memcpy(output + j, input + i, count);
			i += count;
			j += count;
		}
		expanding_zeros = !expanding_zeros;
	}
	return j;
}

void rewind_queue_free(struct RewindQueue *queue) {
	if (queue == NULL)
		return;
	free(queue->snapshot_latest);
	free(queue->snapshot_diff_buffer);
	free(queue->encode_decode_buffer);
	free(queue);
}

struct RewindQueue *rewind_queue_create(size_t item_size) {
	struct RewindQueue *queue = malloc(sizeof(struct RewindQueue));
	if (queue == NULL) return NULL;
	memset(queue, 0, sizeof(struct RewindQueue));
	queue->snapshot_size = item_size;
	queue->snapshot_latest = malloc(item_size);
	queue->encode_decode_buffer = malloc(run_length_upper_bound(item_size));
	queue->snapshot_diff_buffer = malloc(item_size);
	if (!queue->snapshot_latest ||
	    !queue->encode_decode_buffer ||
	    !queue->snapshot_diff_buffer) {
		rewind_queue_free(queue);
		return NULL;
	}
	return queue;
}

void rewind_queue_reset(struct RewindQueue *queue) {
	queue->index_current = 0;
	queue->index_length = 0;
	queue->allocated_data = 0;
	queue->snapshot_latest_is_taken = false;
}

void rewind_queue_pop(struct RewindQueue *queue, struct HagemuGB *gb) {
	if (!queue->snapshot_latest_is_taken)
		return;

	if (queue->index_length == 0) {
		hagemu_snapshot_restore(gb, queue->snapshot_latest);
		queue->snapshot_latest_is_taken = false;
		return;
	}

	int index_prev = (queue->index_current == 0) ? REWIND_QUEUE_INDEX_CAPACITY - 1 : queue->index_current - 1;
	struct RewindQueueIndex latest = queue->index[index_prev];

	memcpy(queue->encode_decode_buffer, queue->data + latest.pos, latest.size);
	queue->index_length--;
	queue->index_current = index_prev;
	queue->allocated_data -= latest.size;

	run_length_decode(queue->snapshot_diff_buffer, queue->snapshot_size, queue->encode_decode_buffer, latest.size);

	for (size_t i = 0; i < queue->snapshot_size; i++) {
		queue->snapshot_latest[i] ^= queue->snapshot_diff_buffer[i];
	}

	hagemu_snapshot_restore(gb, queue->snapshot_latest);
}

void rewind_queue_push(struct RewindQueue *queue, const struct HagemuGB *gb) {
	if (!queue->snapshot_latest_is_taken) {
		hagemu_snapshot_record(gb, queue->snapshot_latest);
		queue->snapshot_latest_is_taken = true;
		return;
	}

	printf("Rewind Queue Size: %zu bytes out of %zu bytes\n", queue->allocated_data, sizeof(queue->data));

	memcpy(queue->snapshot_diff_buffer, queue->snapshot_latest, queue->snapshot_size);
	hagemu_snapshot_record(gb, queue->snapshot_latest);
	for (size_t i = 0; i < queue->snapshot_size; i++) {
		queue->snapshot_diff_buffer[i] ^= queue->snapshot_latest[i];
	}

	size_t upper_bound = run_length_upper_bound(queue->snapshot_size);
	size_t compressed_size = run_length_encode(queue->encode_decode_buffer, upper_bound,
						   queue->snapshot_diff_buffer, queue->snapshot_size);

	if (queue->index_length == 0) {
		memcpy(queue->data, queue->encode_decode_buffer, compressed_size);
		queue->index[0].pos = 0;
		queue->index[0].size = compressed_size;
		queue->index_current = 1;
		queue->index_length = 1;
		queue->allocated_data += compressed_size;
	} else {
		int index_prev = (queue->index_current == 0) ? REWIND_QUEUE_INDEX_CAPACITY - 1 : queue->index_current - 1;
		struct RewindQueueIndex new_index = {
			.pos = queue->index[index_prev].pos + queue->index[index_prev].size,
			.size = compressed_size,
		};

		int oldest_index = queue->index_current - queue->index_length;
		oldest_index = (oldest_index + REWIND_QUEUE_INDEX_CAPACITY) % REWIND_QUEUE_INDEX_CAPACITY;

		if (new_index.pos + new_index.size > REWIND_QUEUE_DATA_CAPACITY) {
			while (queue->index_length > 0) {
				if (queue->index[oldest_index].pos >= new_index.pos) {
					queue->allocated_data -= queue->index[oldest_index].size;
					queue->index_length--;
					oldest_index++;
					oldest_index %= REWIND_QUEUE_INDEX_CAPACITY;
				} else {
					break;
				}
			}
			new_index.pos = 0;
		}
		memcpy(queue->data + new_index.pos, queue->encode_decode_buffer, new_index.size);
		queue->allocated_data += new_index.size;

		while (queue->index_length > 0) {
			if (queue->index[oldest_index].pos < new_index.pos + new_index.size &&
			    queue->index[oldest_index].pos + queue->index[oldest_index].size > new_index.pos) {
				queue->allocated_data -= queue->index[oldest_index].size;
				queue->index_length--;
				oldest_index++;
				oldest_index %= REWIND_QUEUE_INDEX_CAPACITY;
			} else {
				break;
			}
		}

		queue->index[queue->index_current] = new_index;
		if (queue->index_length < REWIND_QUEUE_INDEX_CAPACITY)
			queue->index_length++;
		queue->index_current++;
		queue->index_current %= REWIND_QUEUE_INDEX_CAPACITY;
	}
}

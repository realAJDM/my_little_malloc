#include <stdio.h>
#include <stdlib.h>

#include "mymalloc.h"

#define MEMLENGTH 4096

// Global memory array
static double global_arr[MEMLENGTH];

// Align size to the nearest multiple of 8 (bytes) by rounding
#define ALIGNMENT(x) (((x) + 7) & ~7)

// Metadata structure for each block
// size: size of the block. in_use: allocation status
typedef struct {
	size_t size;
	int in_use;
} meta;

// Size of metadata header aligned to multiple of 8 (bytes)
// This is the overhead for each block
// 1. size_t size. 2. int in_use for block allocation status: FREE 0 or IN_USE 1
#define META_SIZE ALIGNMENT(sizeof(meta))

// Enum for block status
// FREE = 0, IN_USE = 1
enum mem_status {
	FREE = 0,
	IN_USE = 1
};

// Payload is just a char array
// Actual size will be determined at runtime
typedef char blockpayload;

// header (metadata) - size (size_t), in_use (int)
// payload (char array) - actual size determined at runtime
typedef struct {
	meta header;
	blockpayload payload;
} block;

// Check size of header by first int
size_t getBlockSize(meta *head) {
	return head->size;
}

// Set the size of a block
void setBlockSize(meta *head, size_t size) {
	return head->size = size;
}

// Checks if a block is in use by checking the in_use int
// Returns 1 if in use, 0 if free
int isBlockInUse(meta *head) {
	return head->in_use == IN_USE;
}

// Sets the block's in_use status to FREE (0)
void setBlockFree(meta *head) {
	head->in_use = FREE;
}

// Sets the blocks in_use status to IN_USE (1)
void setBlockInUse(meta *head) {
	head->in_use = IN_USE;
}

// Returns the ptr to the first byte of the payload given the ptr to the metadata
// This is done by moving the ptr forward by the size of the metadata (header)
blockpayload *getPayload(meta *head) {
	return (blockpayload *)(((char*)(head)) + META_SIZE);
}

// Returns the ptr to the next block given the ptr to the metadata
// This is done by moving the ptr forward by the size of the metadata (header) + the size of the block (payload)
block *getNextBlock(meta *head) {
	return (block *)(((char*)(head)) + META_SIZE + getBlockSize(head));
}

int getBlockStatus() {
	meta* current = (meta *) global_arr;
	int free_blocks = isBlockInUse(current) != IN_USE;
	size_t block_size = getBlockSize(current);

	// Check if the entire memory is a single free block
	// If so, return 1 (true)
	if ((block_size == (MEMLENGTH * sizeof(double))) && free_blocks) {
		return 1;
	}

	return 0;
}

int coalesce(meta* head) {
	meta* current = head;
	int coalesced = 0;

	while ((char*)current < (char*)global_arr + MEMLENGTH * sizeof(double)) {
		meta* next = (meta*)getNextBlock(current);

		// Check if the next block is within bounds
		if ((char*)next >= (char*)global_arr + MEMLENGTH * sizeof(double)) {
			break;
		}

		// If both current and next blocks are free, merge them
		if (!isBlockInUse(current) && !isBlockInUse(next)) {
			size_t new_size = getBlockSize(current) + META_SIZE + getBlockSize(next);
			setBlockSize(current, new_size);
			coalesced = 1; // Indicate that coalescing occurred
		} else {
			current = next; // Move to the next block
		}
	}

	return coalesced;
}

void *mymalloc(size_t size, char *filename, int line) {
	// Check for zero size allocation
	if (size <= 0 || size > (MEMLENGTH * sizeof(double)) - META_SIZE) {
		fprintf(stderr, "Error: Invalid allocation size %zu at %s: %d\n", size, filename, line);
		return NULL;
	}

	// Setup current ptr to start of global array
	meta *current = (meta * ) global_arr;
	size = ALIGNMENT(size);  // Align size to nearest multiple of 8 (bytes)
	size_t total_size = META_SIZE + size;

	size_t block_size = MEMLENGTH * sizeof(double);
	// Initialize the memory on the first call
	if (getBlockSize(current) == 0 && getBlockStatus(current)) {
		// Initialize the entire memory as a single free block
		setBlockSize(current, block_size - META_SIZE);
		setBlockFree(current);
	}

	size_t current_byte = 0;

	// Traverse the memory blocks to find a suitable free block
	while (current_byte < block_size) {
		size_t curr_size = getBlockSize(current);
		if (isBlockInUse(current) != IN_USE && curr_size >= total_size) {
			size_t remaining_size = curr_size - total_size;

			// Allocate the block
			setBlockInUse(current);
			setBlockSize(current, total_size);

			// Found a suitable free block
			if (remaining_size >= META_SIZE + 8) {
				// Split the block if the remaining size is enough for a new block
				block *next_block = getNextBlock(current);
				setBlockSize(&(next_block->header), remaining_size);
				setBlockFree(&(next_block->header));
			} else if (remaining_size > 0) {
				// If the remaining size is too small to form a new block, allocate the entire block
				setBlockSize(current, curr_size);
			}

			// Return ptr to the payload
			return (void *)getPayload(current);
		}

		else {
			// Move to the next block
			current_byte += getBlockSize(current);
			current = &(getNextBlock(current)->header);
		}
	}

	fprintf(stderr, "Error: Out of memory when trying to allocate %zu bytes at %s: %d\n", size, filename, line);
	return NULL;

}

void myfree(void *ptr, char *filename, int line) {
	// Check for NULL ptr
	if (ptr == NULL) {
		fprintf(stderr, "Error: Attempt to free NULL ptr at %s: %d\n", filename, line);
		return;
	}

	// Get the metadata header from the payload ptr
	meta *current = (meta*)global_arr;
	meta *prev = NULL;
	int found = 0;
	size_t block_size = MEMLENGTH * sizeof(double);
	size_t current_byte = 0;

	// Traverse the memory blocks to find the target block
	while (current_byte < block_size) {
		blockpayload *payload = getPayload(current);
		if (payload == (blockpayload *) ptr) {
			// See if the block is already free
			if (isBlockInUse(current) != IN_USE) {
				fprintf(stderr, "Error: Double free detected at %s: %d\n", filename, line);
				return;
			}

			// Free the block
			setBlockFree(current);
			found = 1;
		}

		// Coalesce if the block right beside the current block is free
		coalesce(current);
		if (coalesce(prev) && prev != NULL) {
			current = prev; // Move current back to prev if coalescing occurred
		}

		// Move to the next block
		current_byte += getBlockSize(current);  // Update current byte position in memory
		prev = current;  // Update prev to current before moving forward
		current = &(getNextBlock(current)->header);  // Move to the next block
	}

	if (found != 1) {
		fprintf(stderr, "Error: Pointer not found in allocated memory at %s: %d\n", filename, line);
	}
	return;
}


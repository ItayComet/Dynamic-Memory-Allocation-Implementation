#include <stddef.h>
#include <unistd.h>
#include <stdio.h>


#define min(x,y) (x > y)? y : x;
#define max(x,y) (x > y)? x : y;

#define MIN_BLOCK_SIZE 32

/*
 * Each block will have a header, which will contain its meta data, before the actual data.
 * A Header contains pointers to the next and previous block headers.
 * Note that if we want the actual data address and we have the Header address, we can just
 * get it by using header + 1, and if we have the actual address and we want to get the header
 * address, we can get it by using (header*) address - 1.
 * size holds the size of the actual data in the block, the actual size allocated is (sizeof(Header) + size)
 * free will be used as a boolean and will indicate that the block is not in use if its value is non zero.
 */
typedef struct Header{
	struct Header *next;
	struct Header *prev;
	size_t size;
	char free;
} Header;

Header* first = NULL;

/*
 * This function assumes the two headers are of adjacent and different blocks and the second block is free.
 * If the two blocks are not adjacent, or the second block isn't free, it will lead to memory overriding.
 * The combined size of the data includes size of the second header as well, because it's allocated and not in use anymore.
 * The new block will have the same free value as the first block.
 */
Header* merge_blocks(Header *head1, Header *head2){
	Header* first_header = min(head1, head2);
	Header* second_header = max(head1, head2);
	first_header -> size = sizeof(Header) + (head1->size) + (head2->size);
	first_header -> next = second_header -> next;
	if(second_header->next)
		(second_header->next)->prev = first_header;
	return first_header;
}

/*
 * This function trims a block to the size of new_size and turns the remaining data into a new free block
 * If the new block is too small, the function will return a NULL pointer and the block will remain as is.
 * The new block will be added to the list, between header's block and the next one.
 * The new block will be flagged as a free block.
 *
 */
Header* split_block(Header *header, size_t new_size){
	size_t new_block_size = header->size - new_size - sizeof(header);
	if(new_block_size < MIN_BLOCK_SIZE)
		return NULL;
	Header* new_header = (Header*)((void*)(header + 1) + new_size);
	new_header -> prev = header;
	new_header -> next = header -> next;
	new_header -> size = new_block_size;
	new_header -> free = 1;
	(header->next)->prev = new_header;
	header -> next = new_header;
	return new_header;
}


/*
 * This implementation of malloc is based on the first-fit technique:
 * Starting from the first block, we pass through each block, and if it's free
 * we'll merge it with all next free blocks so the free spaces will not be segmented.
 * Each time we'll get to a free block (after we merged it with the next free blocks)
 * we'll check if its size is big enough to allocate the new block into. If it is,
 * we'll split the free block, so we'll use only the space we need, turn off the free flag
 * and return the address.
 * If we reached the end of the list it means there is no free segment large enough to hold
 * the requested size, then we'll use the sbrk system call to move the heap pointer and we'll
 * initialize a new block.
 */
void *malloc(size_t size){
	Header *last, *curr, *new_block;

	if(size < MIN_BLOCK_SIZE)
		size = MIN_BLOCK_SIZE;

	/*find first fit*/
	curr = first;
	last = NULL;
	while(curr != NULL){
		while(1){//merge the current sequence of free blocks
			if(curr->free && curr->next != NULL && (curr->next)->free)
				curr = merge_blocks(curr, curr->next);
			else
				break;
		}
		if(curr->free && curr->size >= size){
			split_block(curr, size);
			curr->free = 0;
			return curr + 1;
		}
		last = curr;
		curr = curr->next;
	}

	/*no fit - extend heap*/
	new_block = (Header*)sbrk(size + sizeof(Header));
	new_block->prev = last;
	new_block->next = NULL;
	new_block->size = size;
	new_block->free = 0;
	if(first == NULL)
		first = new_block;
	return (void*)(new_block + 1);
}

/*
 * This implementation of free will flag the block corresponding to the given address as free.
 * If the block is the last in the list of blocks, the function will decrease the stack pointer,
 * down to the last non free block.
 */
void free(void* address){
	Header* curr;
	if(address == NULL)
		return;
	Header* header = (Header*)address - 1;
	header->free = 1;
	if(header->next == NULL){	//last block in the list, we can decrease the heap pointer
		curr = header;
		while(curr != first){
			if((curr->prev)->free)
				curr = curr->prev;
			else
				break;
		}
		if (curr == first)	//all blocks are free
			first = NULL;
		brk(curr);
	}
}


/*
 * This implementation of free() will de-allocate a block only if its address is correct,
 * therefore it's safe to put in any address.
 */
void free_with_caution(void *address){
	Header* curr = first;
	if(address == NULL)
		return;
	Header* header = (Header*)address - 1;
	while(curr){
		if(curr == header){
			curr->free = 1;
			if(curr->next == NULL){	//last block in the list, we can decrease the heap pointer
				while(curr != first){
					if((curr->prev)->free)
						curr = curr->prev;
					else
						break;
				}
				if (curr == first)	//all blocks are free
						first = NULL;
				brk(curr);
			}
			break;
		}
		curr = curr->next;
	}
}

/*
 * A simple function to copy data from one block to another, assumes the dst block is larger
 */
void copy_data(Header* src, Header* dst){
	size_t i;
	char* src_block = (void*)(src + 1);
	char* dst_block = (void*)(dst + 1);
	for(i = 0; i < src->size; i++)
		*(dst_block + i) = *(src_block + i);
}

/*
 * TBA
 */
void *realloc(void *address, size_t size){
	if(address == NULL)
		return NULL;
	Header *header = (Header*)address - 1;
	size_t size_diff = size - header->size;
	if(size_diff < 0){
		split_block(header, size);
		return address;
	}

	/*End of the list, we'll move the heap pointer*/
	if(header->next == NULL){
		sbrk(size_diff);
		header->size = size;
		return address;
	}

	/*Check if we can take space from the next block*/
	if((header->next)->free){
		while(((header->next)->next)->free)
			merge_blocks(header->next, (header->next)->next);
		if((header->next)->size >= size_diff){
			split_block(header->next, size_diff);
			merge_blocks(header, header->next);//it will have some extra memory allocated
			return address;
		}
	}

	/*Use malloc to reallocate, then copy the contents and free old address*/
	void *new_address = malloc(size);
	if(!new_address)
		return NULL;
	copy_data(header, (Header*)new_address - 1);
	free(address);
	return new_address;

}

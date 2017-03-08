#include "half_fit.h"
#include <lpc17xx.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include "type.h"
#include "uart.h"

//---------------------------------------CONSTANTS------------------------------------//
#define STARTMEM 0x10000000
#define CHUNKPTRS 0x00000000

const int MAX_SIZE = 1024*32;
const int BUCKET_SIZE = 11;
const int BASE_CHUNK_SIZE = 32;
const int MAX_CHUNKS = 1024;


//-----------------------------------Structs/skeletons--------------------------------//
unsigned char array[MAX_SIZE]__attribute__((section(".ARM.__at_0x10000000"), zero_init));
int bucket_flag[BUCKET_SIZE] = {0};
int current_memory_block_size = MAX_SIZE;//use this to keep track of prev blocks memory 

typedef struct memoryBlockHeader{//initializes memory for headers
	U32 prev : 10;
	U32 next : 10;
	U32 size : 10;
	U32 is_alloc : 1;
	U32 padding : 1;
} block_header;

typedef struct unalloc_header{
	U32 prev_unalloc : 10;
	U32 next_unalloc : 10;
} unalloc_header;


block_header *start = (block_header*)array;//puts mem to the first memory address of our bucket
block_header *bucket[BUCKET_SIZE] = {NULL};
unalloc_header *unalloc_start_ptr;

//---------------------------------------Helpers-------------------------------------//

void print_bucket_structure(){
	int i;
	for (i = 0; i < 11; i++){
		printf("index %d addr: %d size: %d ",i, bucket[i],bucket[i]->size);
		printf("flag: %d \n", bucket_flag[i]);
	}
	printf("\n");
	printf("\n");
	printf("\n");
	printf("\n");
}
//------------------------------------UPDATE-----------------------------------------//
void update_header(block_header *ptr, int new_size, int new_next, int new_prev, bool alloc_or_not){
	ptr->size = new_size;
	ptr->next = new_next;
	ptr->prev = new_prev;
	ptr->is_alloc = alloc_or_not;
}

void pop_unalloc_ptrs(int buck_index){
	unalloc_header *next_ptr = (unalloc_header*) bucket[buck_index]->next + 8;
	unalloc_header *old_ptr = (unalloc_header*) bucket[buck_index] + 8; 
	if (next_ptr == NULL){
		bucket[buck_index] = CHUNKPTRS;
		bucket_flag[buck_index] = 0;
	}
	else{
		
		next_ptr->prev_unalloc = NULL;
		bucket[buck_index] = (block_header*) next_ptr - 8;
		old_ptr->next_unalloc = NULL;		
		
	}
	
}

void push_unalloc_ptrs(block_header* ptr, int buck_index){
	unalloc_header *curr_ptr = (unalloc_header*) bucket[buck_index] + 8;
	unalloc_header *fir_ptr = (unalloc_header*) ptr + 8; 
	fir_ptr->next_unalloc = (U32) bucket[buck_index];
	bucket[buck_index] = (block_header*) ptr;
	curr_ptr->prev_unalloc = (U32) ptr;
}

void delete_unalloc_ptrs(block_header* block, int i){
		unalloc_header *u_next = (unalloc_header*)block + 8;
		//print_bucket_structure();
		if(u_next -> next_unalloc != NULL){
			pop_unalloc_ptrs(i);
		}
		else{
 		bucket[i] = NULL;
 		bucket_flag[i] = 0;
		}
}



void update_bucket_ptrs(block_header *new_ptr, block_header *old_ptr , int chunk_size){
	int i;
	int range = 1;
	int counter = -2;
	unalloc_header *ptr_to_unalloc;
	block_header ptr_block;
	block_header *__block;
	
	if (chunk_size == 0){//max memory is allocated
		//sets all bucket values to nothing
		memset(bucket, NULL, BUCKET_SIZE * sizeof(bucket[0]));
		memset(bucket_flag, 0, BUCKET_SIZE * sizeof(bucket_flag[0]));
		return;
	}
	if (chunk_size == 1024){//max size is freed
		memset(bucket, NULL, BUCKET_SIZE * sizeof(bucket[0]));
		memset(bucket_flag, 0, BUCKET_SIZE * sizeof(bucket_flag[0]));
		bucket_flag[10] = 1;
		bucket[10] = new_ptr;
		bucket[10]->size = 1023;
		bucket[10]->next = NULL;
		bucket[10]->prev = NULL; 
		bucket[10]->padding = 1;//means next is pointing to nothing
		return;
	}
	
	
	if (old_ptr != NULL){// deletes old ptr
		for (i = BUCKET_SIZE; i >= 0; i--){
			if(old_ptr != NULL && bucket[i] == old_ptr){//must change logic to support doubly linked list for ptrs (bucket[i] == old_ptr) before!!
				__block = bucket[i];	
				delete_unalloc_ptrs(__block, i);
				break;
			}
		}
	}
	
	// REFERENCED https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2	to find this algorithm that finds the next highest power of 2 for chunk size to allocate
	chunk_size--;
	chunk_size |= chunk_size >> 1;
	chunk_size |= chunk_size >> 2;
	chunk_size |= chunk_size >> 4;
	chunk_size |= chunk_size >> 8;
	chunk_size |= chunk_size >> 16;
	chunk_size++;
	
	while(chunk_size != 0){
		chunk_size = chunk_size >> 1;
		counter++;		
	}
	for (i = counter; i < BUCKET_SIZE; i++){
		if (bucket_flag[i] == 1){
			push_unalloc_ptrs(new_ptr, i);
			break;
		}
		else if (bucket_flag[i] == 0){
			bucket[i] = new_ptr;
			bucket_flag[i] = 1;
			break;
		}
	}	
}

void update_alloc_to_unalloc(block_header *block, U32 size_of_unalloc, U32 next_unalloc_ptr, U32 prev_unalloc_ptr) {
	unalloc_header *ptrs;
	ptrs = (unalloc_header*)block + 8;
	ptrs -> prev_unalloc = NULL;
	ptrs -> next_unalloc = NULL;
	block->is_alloc = 0;
	block->size = size_of_unalloc;
}

//------------------------------------CREATE-----------------------------------------//


//create the header for the unallocated blocks
void create_unalloc_block(block_header *current_ptr, int unalloc_size, int allocated_size){
	block_header* unalloc_head;
	unalloc_header *extra_header;
	int curr_address;
	BOOL flag = FALSE;
	

	
	if (unalloc_size == 0){//if no more memory left, just return
		flag = TRUE;
	}
	
	if (!flag) {
		current_ptr-> next = ((int)(current_ptr) >> 5) + allocated_size;
		
		//if (current -> padding == 1) {
		//	unalloc_head = (block_header*) (current_ptr->next << 5);  // null will go to start of array 1000000
		//} else {
			unalloc_head = (block_header*) ((int)&array | ((int)current_ptr->next << 5));  // null will go to start of array 0000000
		//}
		unalloc_head->size = unalloc_size + 1;
		unalloc_head->is_alloc = 0;
		unalloc_head->prev = (int) current_ptr >> 5;
		unalloc_head->next = CHUNKPTRS;
		
		
		// extra header
		extra_header = (unalloc_header*) current_ptr->next + 1;
		extra_header->prev_unalloc = NULL;
		extra_header->next_unalloc = NULL;
		
		update_bucket_ptrs(unalloc_head, current_ptr, unalloc_size);
	}
}

// create the header for the allocated blocks
block_header *create_alloc_block(block_header *current_ptr, int alloc_size){
	int unalloc_size;
	int total_size = current_ptr->size;
	if(alloc_size == 0){
		return current_ptr;
	}
	else if(alloc_size == 1024){ // Max case
		alloc_size = 0;  // setting 0 to fit 1024(11 bit number)
		current_ptr->is_alloc = 1;
		update_bucket_ptrs(current_ptr, NULL, 0);
		return current_ptr;  
	}
	unalloc_size = total_size - alloc_size;//cnw
	current_ptr->size = alloc_size;
	current_ptr->is_alloc = 1;
	create_unalloc_block(current_ptr, unalloc_size, alloc_size);
	return current_ptr;
}

//------------------------------------SEARCH-----------------------------------------//
int find_bucket_index(int chunk){ //returns the index of the bucket pointer
 	int i;
	int range = 1024;
	int lowest_index;
	
	for (i = BUCKET_SIZE - 1; i >= 0 ; i--){
		if (chunk <= range){ //compare range & chunk?
			//printf("bucketflag %d: %d\n",i, bucket_flag[i]);
				if (bucket_flag[i]  == 1){//check if next bucket[index] is empty
					lowest_index = i;
				}		
		}
		range /= 2; //shifts bit left
	}
	return lowest_index;
}



//------------------------------------DELETE-----------------------------------------//

void delete_header(block_header *header_ptr){//sets everything to null to avoid loose memory 
	header_ptr->next = NULL;
	header_ptr->prev = NULL;
	header_ptr->size = NULL;
	header_ptr->is_alloc = 0; // NULL?
}


//------------------------------------OTHER HELPERS-----------------------------------------//
void coaellece(block_header *left, block_header *right) {
	//right pointer is the large block of memory to update
	//left pointer is the block of memory that will merge with right to form big block
	int i;
	int temp_left = ((int)(left) >> 5); 
	int temp_left_prev = ((int)(left->prev) >> 5);
	
	block_header *right_bound = (block_header*) ((int)&array | ((int)right->next) << 5);
	if (right_bound->is_alloc == 0) {
		coaellece(right, right_bound);
	}
	
	for (i = BUCKET_SIZE; i >= 0; i--){
			if(bucket[i] == left){//must change logic to support doubly linked list for ptrs (bucket[i] == old_ptr) before!!	
			delete_unalloc_ptrs(left, i);
			break;
		}
	}
	
	left = (block_header*) ((int)&array | ((int)temp_left) << 5); 
	
	left->size += right->size;
	left->next = right->next;
	left->prev = temp_left_prev;
	left->is_alloc = 0;
	
	update_bucket_ptrs(left ,NULL, left->size);
	delete_header(right);
}

//------------------------------------MAIN FUNCTIONS-----------------------------------------//

void  half_init(void){
	block_header* inital_block = (block_header*)array; //sets the memory to start inside array

	inital_block->next = CHUNKPTRS;
	inital_block->prev = CHUNKPTRS;
	inital_block->size = 1023;
	inital_block->is_alloc = 0;
	inital_block->padding = NULL;
	memset(bucket, NULL, BUCKET_SIZE * sizeof(bucket[0]));
	memset(bucket_flag, 0, BUCKET_SIZE * sizeof(bucket_flag[0]));
	
	inital_block = (inital_block + 1); // linked list for from the bucket ptrs 1 byte from the header
	inital_block->next = CHUNKPTRS;
	inital_block->prev = CHUNKPTRS;
	bucket[10] = (block_header*) STARTMEM; 
	bucket_flag[10] = 1;

}
	
void *half_alloc(unsigned int size){
	int	alloc_chunks = ((size+4) % 32 == 0)? (size+4)/32 : (size+4)/32 + 1;
	int bucket_index; 
	block_header *allocated_block;
	
	//max_block case: can not handle the size of more than 32764 
	if (size > 32764){ 
		return NULL;
	}
	 bucket_index = find_bucket_index(alloc_chunks);
	//case 1: memory request not available
	
	if (bucket_index == NULL){
		return (block_header*)STARTMEM;
	}
	else{
		allocated_block = create_alloc_block(bucket[bucket_index], alloc_chunks);
	}

	return allocated_block;
}

void  half_free(void * address){
	
	block_header *header_to_free = address;
	int size_of_addr = header_to_free ->size;
	block_header *next_to_htfree =  (block_header*) ((int)&array | ((int)header_to_free->next) << 5);
	block_header *next_to_next = (block_header*) ((int)&array | ((int)next_to_htfree->next) << 5);
	block_header *prev_to_htfree =  (block_header*) ((int)&array | ((int)header_to_free->prev) << 5); 
	block_header *prev_to_prev = (block_header*) ((int)&array | ((int)prev_to_htfree->prev) << 5);
	
	
	if(prev_to_htfree == (block_header*)STARTMEM){
		prev_to_prev = (block_header*) (block_header*) ((int)CHUNKPTRS | ((int)header_to_free->prev));
	}

	
	
	if (size_of_addr == 1023){ //delete the whole block
		update_header(header_to_free, MAX_SIZE, NULL, NULL, 0);
		update_bucket_ptrs(header_to_free, NULL, MAX_CHUNKS); 	
	}
	else if((next_to_htfree != NULL && next_to_htfree->is_alloc == 1) && (prev_to_htfree != NULL && prev_to_htfree->is_alloc == 1)){ //1
		update_alloc_to_unalloc(header_to_free, size_of_addr,NULL, NULL );
		update_bucket_ptrs(header_to_free,NULL,size_of_addr);
		
	}
	else if((next_to_htfree != NULL && next_to_htfree->is_alloc == 0) && (prev_to_htfree != NULL && prev_to_htfree->is_alloc == 1)){ //2
		if(next_to_next != NULL && next_to_next->is_alloc ==1){//2a
			update_alloc_to_unalloc(header_to_free, size_of_addr,NULL, NULL );
			coaellece(header_to_free, next_to_htfree);
		}	
		else if(next_to_next == NULL){ //2b
			update_alloc_to_unalloc(header_to_free, size_of_addr,NULL, NULL );
			coaellece(header_to_free, next_to_htfree);
		}
		else if(next_to_htfree == NULL){//2c
			update_alloc_to_unalloc(header_to_free, size_of_addr,NULL, NULL );
			update_bucket_ptrs(header_to_free,NULL,size_of_addr);
		}

	}
	else if((next_to_htfree != NULL && next_to_htfree->is_alloc == 1) && (prev_to_htfree != NULL && prev_to_htfree->is_alloc == 0)){//3
		if(prev_to_prev != NULL && prev_to_prev->is_alloc ==1){//3a
			update_alloc_to_unalloc(header_to_free, size_of_addr,NULL, NULL );
			coaellece(prev_to_htfree, header_to_free);
		}	
		else if(prev_to_prev == NULL){//3b
			update_alloc_to_unalloc(header_to_free, size_of_addr,NULL, NULL );
			coaellece(prev_to_htfree, header_to_free);
		}
		else if(prev_to_htfree == NULL){ //3c
			update_alloc_to_unalloc(header_to_free, size_of_addr,NULL, NULL );
			update_bucket_ptrs(header_to_free,NULL,size_of_addr);
		}
	}
	else if((next_to_htfree != NULL && next_to_htfree->is_alloc == 0) && (prev_to_htfree != NULL && prev_to_htfree->is_alloc == 0)){//4a
		if(next_to_next != NULL && next_to_next->is_alloc == 1 && prev_to_prev != NULL && prev_to_prev->is_alloc == 1){
			update_alloc_to_unalloc(header_to_free, size_of_addr,NULL, NULL );
			coaellece(prev_to_htfree, header_to_free);
		}
		else if(next_to_next == NULL && prev_to_prev == NULL){//4b
			update_alloc_to_unalloc(header_to_free, size_of_addr,NULL, NULL );
			coaellece(prev_to_htfree, header_to_free);
		}
	}
	
	
}


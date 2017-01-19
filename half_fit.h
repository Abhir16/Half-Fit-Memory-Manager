#ifndef HALF_FIT_H_
#define HALF_FIT_H_

/*
 * Author names:
 *   1.  uWaterloo User ID:  a5ravi@uwaterloo.ca
 *   2.  uWaterloo User ID:  r2lad@uwaterloo.ca
 */

#define smlst_blk                       5 
#define smlst_blk_sz  ( 1 << smlst_blk ) 
#define lrgst_blk                       15 
#define lrgst_blk_sz    ( 1 << lrgst_blk ) 


void  half_init( void );
void *half_alloc( unsigned int );
void  half_free( void * );



#endif


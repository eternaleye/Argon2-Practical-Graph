/*
 * Argon2 source code package
 *
 * Written by Daniel Dinu and Dmitry Khovratovich, 2015
 *
 * This work is licensed under a Creative Commons CC0 1.0 License/Waiver.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with
 * this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#pragma once
#ifndef ARGON2_CORE_H
#define ARGON2_CORE_H

/*************************Argon2 internal
 * constants**************************************************/

enum Argon2_core_constants {
  /* Version of the algorithm */
  ARGON2_VERSION_NUMBER = 0x10,

  /* Memory block size in bytes */
  ARGON2_BLOCK_SIZE = 1024,
  ARGON2_WORDS_IN_BLOCK = ARGON2_BLOCK_SIZE / 8,
  ARGON2_QWORDS_IN_BLOCK = 64,

  /* Number of pseudo-random values generated by one call to Blake in Argon2i to
     generate reference block positions */
  ARGON2_ADDRESSES_IN_BLOCK = 128,

  /* Pre-hashing digest length and its extension*/
  ARGON2_PREHASH_DIGEST_LENGTH = 64,
  ARGON2_PREHASH_SEED_LENGTH = 72
};

/* Argon2 primitive type */
typedef enum _Argon2_type {
  Argon2_d = 0,
  Argon2_i = 1,
} Argon2_type;

/*************************Argon2 internal data
 * types**************************************************/

/*
 * Structure for the (1KB) memory block implemented as 128 64-bit words.
 * Memory blocks can be copied, XORed. Internal words can be accessed by [] (no
 * bounds checking).
 */
#ifndef _MSC_VER
typedef struct _block {
  uint64_t v[ARGON2_WORDS_IN_BLOCK];
} __attribute__((aligned(16))) block;
#else
typedef struct _block {
  uint64_t v[ARGON2_WORDS_IN_BLOCK];
} __declspec(align(16)) block;
#endif

/*****************Functions that work with the block******************/

// Initialize each byte of the block with @in
extern void init_block_value(block *b, uint8_t in);

// Copy block @src to block @dst
extern void copy_block(block *dst, const block *src);

// XOR @src onto @dst bytewise
extern void xor_block(block *dst, const block *src);

/*
 * Argon2 instance: memory pointer, number of passes, amount of memory, type,
 * and derived values.
 * Used to evaluate the number and location of blocks to construct in each
 * thread
 */
typedef struct _Argon2_instance_t {
  block *memory;                // Memory pointer
  const uint32_t passes;        // Number of passes
  const uint32_t memory_blocks; // Number of blocks in memory
  const uint32_t segment_length;
  const uint32_t lane_length;
  const uint32_t lanes;
  const uint32_t threads;
  const Argon2_type type;
  const bool print_internals; // whether to print the memory blocks
} Argon2_instance_t;

/*
 * Argon2 position: where we construct the block right now. Used to distribute
 * work between threads.
 */
typedef struct _Argon2_position_t {
  const uint32_t pass;
  const uint32_t lane;
  const uint8_t slice;
  uint32_t index;
} Argon2_position_t;

/*Struct that holds the inputs for thread handling FillSegment*/
typedef struct _Argon2_thread_data {
  Argon2_instance_t *instance_ptr;
  Argon2_position_t pos;
} Argon2_thread_data;

/*Macro for endianness conversion*/

#if defined(_MSC_VER)
#define BSWAP32(x) _byteswap_ulong(x)
#else
#define BSWAP32(x) __builtin_bswap32(x)
#endif

/*************************Argon2 core
 * functions**************************************************/

/* Allocates memory to the given pointer
 * @param memory pointer to the pointer to the memory
 * @param m_cost number of blocks to allocate in the memory
 * @return ARGON2_OK if @memory is a valid pointer and memory is allocated
 */
int allocate_memory(block **memory, uint32_t m_cost);

/* Clears memory
 * @param instance pointer to the current instance
 * @param clear_memory indicates if we clear the memory with zeros.
 */
void clear_memory(Argon2_instance_t *instance, bool clear);

/* Deallocates memory
 * @param memory pointer to the blocks
 */
void free_memory(block *memory);

/*
 * Computes absolute position of reference block in the lane following a skewed
 * distribution and using a pseudo-random value as input
 * @param instance Pointer to the current instance
 * @param position Pointer to the current position
 * @param pseudo_rand 32-bit pseudo-random value used to determine the position
 * @param same_lane Indicates if the block will be taken from the current lane.
 * If so we can reference the current segment
 * @pre All pointers must be valid
 */
uint32_t index_alpha(const Argon2_instance_t *instance,
                     const Argon2_position_t *position, uint32_t pseudo_rand,
                     bool same_lane);

/*
 * Function that validates all inputs against predefined restrictions and return
 * an error code
 * @param context Pointer to current Argon2 context
 * @return ARGON2_OK if everything is all right, otherwise one of error codes
 * (all defined in <argon2.h>
 */
int validate_inputs(const Argon2_Context *context);

/*
 * Hashes all the inputs into @a blockhash[PREHASH_DIGEST_LENGTH], clears
 * password and secret if needed
 * @param  context  Pointer to the Argon2 internal structure containing memory
 * pointer, and parameters for time and space requirements.
 * @param  blockhash Buffer for pre-hashing digest
 * @param  type Argon2 type
 * @pre    @a blockhash must have at least @a PREHASH_DIGEST_LENGTH bytes
 * allocated
 */
void initial_hash(uint8_t *blockhash, Argon2_Context *context,
                  Argon2_type type);

/*
 * Function creates first 2 blocks per lane
 * @param instance Pointer to the current instance
 * @param blockhash Pointer to the pre-hashing digest
 * @pre blockhash must point to @a PREHASH_SEED_LENGTH allocated values
 */
void fill_firsts_blocks(uint8_t *blockhash, const Argon2_instance_t *instance);

/*
 * Function allocates memory, hashes the inputs with Blake,  and creates first
 * two blocks. Returns the pointer to the main memory with 2 blocks per lane
 * initialized
 * @param  context  Pointer to the Argon2 internal structure containing memory
 * pointer, and parameters for time and space requirements.
 * @param  instance Current Argon2 instance
 * @return Zero if successful, -1 if memory failed to allocate. @context->state
 * will be modified if successful.
 */
int initialize(Argon2_instance_t *instance, Argon2_Context *context);

/*
 * XORing the last block of each lane, hashing it, making the tag. Deallocates
 * the memory.
 * @param context Pointer to current Argon2 context (use only the out parameters
 * from it)
 * @param instance Pointer to current instance of Argon2
 * @pre instance->state must point to necessary amount of memory
 * @pre context->out must point to outlen bytes of memory
 * @pre if context->free_cbk is not NULL, it should point to a function that
 * deallocates memory
 */
void finalize(const Argon2_Context *context, Argon2_instance_t *instance);

/*
 * Function that fills the segment using previous segments also from other
 * threads
 * @param instance Pointer to the current instance
 * @param position Current position
 * @pre all block pointers must be valid
 */
extern void fill_segment(const Argon2_instance_t *instance,
                         Argon2_position_t position);

/*
 * Function that fills the entire memory t_cost times based on the first two
 * blocks in each lane
 * @param instance Pointer to the current instance
 */
void fill_memory_blocks(Argon2_instance_t *instance);

/*
 * Function that performs memory-hard hashing with certain degree of parallelism
 * @param  context  Pointer to the Argon2 internal structure
 * @return Error code if smth is wrong, ARGON2_OK otherwise
 */
int argon2_core(Argon2_Context *context, Argon2_type type);

#endif

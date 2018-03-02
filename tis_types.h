#ifndef _TIS_TYPES_
#define _TIS_TYPES_

#include <stddef.h>

/*
 * Begin enums
 */

typedef enum tis_op_type {
    TIS_OP_TYPE_INVALID = 0,
    TIS_OP_TYPE_ADD,
    TIS_OP_TYPE_HCF,
    TIS_OP_TYPE_JEZ,
    TIS_OP_TYPE_JGZ,
    TIS_OP_TYPE_JLZ,
    TIS_OP_TYPE_JMP,
    TIS_OP_TYPE_JNZ,
    TIS_OP_TYPE_JRO,
    TIS_OP_TYPE_MOV,
    TIS_OP_TYPE_NEG,
    TIS_OP_TYPE_NOP,
    TIS_OP_TYPE_SAV,
    TIS_OP_TYPE_SUB,
    TIS_OP_TYPE_SWP,
} tis_op_type_t;

typedef enum tis_op_arg_type {
    TIS_OP_ARG_TYPE_NONE = 0,
    TIS_OP_ARG_TYPE_CONSTANT,
    TIS_OP_ARG_TYPE_REGISTER,
    TIS_OP_ARG_TYPE_LABEL,
} tis_op_arg_type_t;

typedef enum tis_register {
    TIS_REGISTER_INVALID = 0,
    TIS_REGISTER_ACC,
    TIS_REGISTER_BAK,
    TIS_REGISTER_NIL,
    TIS_REGISTER_UP,
    TIS_REGISTER_DOWN,
    TIS_REGISTER_LEFT,
    TIS_REGISTER_RIGHT,
    TIS_REGISTER_ANY,
    TIS_REGISTER_LAST,
} tis_register_t;

typedef enum tis_op_result {
    TIS_OP_RESULT_OK,
    TIS_OP_RESULT_READ_WAIT,
    TIS_OP_RESULT_WRITE_WAIT, // Need to run node again in this tick, to finalize write
    TIS_OP_RESULT_ERR,
} tis_op_result_t;

typedef enum tis_node_state {
    TIS_NODE_STATE_RUNNING,
    TIS_NODE_STATE_READ_WAIT,
    TIS_NODE_STATE_WRITE_WAIT,
    TIS_NODE_STATE_IDLE,
} tis_node_state_t;

typedef enum tis_node_type {
    TIS_NODE_TYPE_DAMAGED = 0,
    TIS_NODE_TYPE_RESERVED, // T20 Reserved
    TIS_NODE_TYPE_COMPUTE, // T21 Basic Execution Node
    TIS_NODE_TYPE_MEMORY_STACK, // T30 Stack Memory Node
    TIS_NODE_TYPE_MEMORY_RAM, // T31 Random Access Memory Node
} tis_node_type_t;

typedef enum tis_io_type {
    TIS_IO_TYPE_INVALID = 0,
    TIS_IO_TYPE_IOSTREAM_ASCII,
    TIS_IO_TYPE_IOSTREAM_NUMERIC,
    TIS_IO_TYPE_OSTREAM_IMAGE,
    TIS_IO_TYPE_IGENERATOR_ALGEBRAIC,
    TIS_IO_TYPE_IGENERATOR_CONSTANT,
    TIS_IO_TYPE_IGENERATOR_CYCLIC,
    TIS_IO_TYPE_IGENERATOR_RANDOM,
    TIS_IO_TYPE_IGENERATOR_SEQUENCE,
} tis_io_type_t;

/*
 * Begin structs
 */

typedef struct tis_op_arg {
    tis_op_arg_type_t type;
    union {
        int con;
        tis_register_t reg;
        char* label;
    };
} tis_op_arg_t;

typedef struct tis_op {
    tis_op_type_t type;
    tis_op_arg_t src;
    tis_op_arg_t dst;
    size_t linenum; // line number in-node, not of source file
    char* linetext; // full text of line
    char* label;
} tis_op_t;

typedef struct tis_node {
    tis_node_type_t type;
    int id; // The id from the source, non-compute nodes are skipped
    size_t row;
    size_t col;
    char* name; // optional (no equivalent in-game)
    tis_op_t* code[15]; // up to 15 lines of code (used by compute)
    int data[15]; // up to 15 cells for data (used by memory)
    int acc; // (used by compute)
    int bak; // (used by compute)
    int writebuf; // (used by compute)
    tis_register_t last; // (used by compute)
    tis_register_t writereg; // UpDownLeftRightAny -> ready, Nil -> complete, Invalid -> quiet (used by compute)
    int index; // for memory nodes is addr, for compute is ip
    tis_node_state_t laststate; // managed externally
} tis_node_t;

typedef struct tis_io_node {
    tis_io_type_t type;
    char* name; // optional
    FILE* file; // TODO union this for other type that don't use a FILE?
} tis_io_node_t;

typedef struct tis {
    size_t rows;
    size_t cols;
    size_t size; // must be equal to rows*cols
    char* name; // optional
    // These are arrays of pointers, so that entries can be NULL
    tis_node_t** nodes; // length = rows*cols = size
    tis_io_node_t** inputs; // length = cols
    tis_io_node_t** outputs; // length = cols
} tis_t;

typedef struct tis_opt {
    int verbose;
} tis_opt_t;
extern tis_opt_t opts;

/*
 * Begin macros
 */

#define safe_free(ptr) do { \
    if(ptr != NULL) {       \
        free(ptr);          \
        ptr = NULL;         \
    }                       \
} while(0)

#define safe_free_list(ptr,len,subfree) do {    \
    if(ptr != NULL) {                           \
        for(size_t opi = 0; opi < len; opi++) { \
            subfree(ptr[opi]);                  \
        }                                       \
        free(ptr);                              \
        ptr = NULL;                             \
    }                                           \
} while(0)

#define safe_free_op(ptr) do {                       \
    if(ptr != NULL) {                                \
        safe_free(ptr->linetext);                    \
        safe_free(ptr->label);                       \
        if(ptr->src.type == TIS_OP_ARG_TYPE_LABEL) { \
            safe_free(ptr->src.label);               \
        }                                            \
        if(ptr->dst.type == TIS_OP_ARG_TYPE_LABEL) { \
            safe_free(ptr->dst.label);               \
        }                                            \
        free(ptr);                                   \
        ptr = NULL;                                  \
    }                                                \
} while(0)

#define safe_free_node(ptr) do {                     \
    if(ptr != NULL) {                                \
        safe_free(ptr->name);                        \
        for(size_t nodei = 0; nodei < 15; nodei++) { \
            safe_free_op(ptr->code[nodei]);          \
        }                                            \
        /* TODO safe_free(ptr->data); */             \
        free(ptr);                                   \
        ptr = NULL;                                  \
    }                                                \
} while(0)

#define safe_free_io_node(ptr) do {      \
    if(ptr != NULL) {                    \
        safe_free(ptr->name);            \
        /* TODO fclose(ptr->file); */    \
        free(ptr);                       \
        ptr = NULL;                      \
    }                                    \
} while(0)

#define spam(...)  do { if(opts.verbose >=  2) { fprintf(stderr, "SPAM:\t"__VA_ARGS__); } } while(0)
#define debug(...) do { if(opts.verbose >=  1) { fprintf(stderr, "DEBUG:\t"__VA_ARGS__); } } while(0)
#define warn(...)  do { if(opts.verbose >=  0) { fprintf(stderr, "WARN:\t"__VA_ARGS__); } } while(0)
#define error(...) do { if(opts.verbose >= -1) { fprintf(stderr, "ERROR:\t"__VA_ARGS__); } } while(0)

#define custom_abort() exit(EXIT_FAILURE)

#endif /* _TIS_TYPES_ */

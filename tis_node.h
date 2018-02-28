#ifndef _TIS_NODE_
#define _TIS_NODE_

#include "tis_types.h"

// TODO Are these correct?
#define TIS_NODE_LINE_COUNT 15
#define TIS_NODE_LINE_LENGTH 19

tis_node_state_t run(tis_t* tis, tis_node_t* node);
tis_node_state_t run_defer(tis_t* tis, tis_node_t* node);

tis_op_result_t read_register(tis_t* tis, tis_node_t* node, tis_register_t reg, int* value);
tis_op_result_t write_register(tis_t* tis, tis_node_t* node, tis_register_t reg, int value);
tis_op_result_t write_register_defer(tis_t* tis, tis_node_t* node, tis_register_t reg);

#endif /* _TIS_NODE_ */

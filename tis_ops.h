#ifndef _TIS_OPS_
#define _TIS_OPS_

#include "tis_types.h"
#include "tis_node.h"

tis_op_result_t step(tis_t* tis, tis_node_t* node, tis_op_t* op);
tis_op_result_t step_defer(tis_t* tis, tis_node_t* node, tis_op_t* op);

#endif /* _TIS_OPS_ */

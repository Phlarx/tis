#ifndef _TIS_IO_
#define _TIS_IO_

#include "tis_types.h"

tis_node_state_t run_input(tis_t* tis, tis_io_node_t* io);
tis_node_state_t run_output(tis_t* tis, tis_io_node_t* io);

tis_node_state_t run_input_defer(tis_t* tis, tis_io_node_t* io);
tis_node_state_t run_output_defer(tis_t* tis, tis_io_node_t* io);

tis_op_result_t input(tis_io_node_t* io, int* value);
tis_op_result_t output(tis_io_node_t* io, int value);

#endif /* _TIS_IO_ */

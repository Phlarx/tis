#ifndef _TIS_IO_
#define _TIS_IO_

#include "tis_types.h"

tis_op_result_t input(tis_io_node_t* io, int* value);
tis_op_result_t output(tis_io_node_t* io, int value);

#endif /* _TIS_IO_ */

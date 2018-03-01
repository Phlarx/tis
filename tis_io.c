
#include <stdio.h>

#include "tis_types.h"

tis_op_result_t input(tis_io_node_t* io, int* value) {
    if(io == NULL) {
        return TIS_OP_RESULT_READ_WAIT;
    }
    int in = EOF;
    switch(io->type) {
        case TIS_IO_TYPE_IOSTREAM_ASCII:
            in = fgetc(io->file);
            if(in == EOF) {
                return TIS_OP_RESULT_READ_WAIT;
            }
            *value = in;
            break;
        case TIS_IO_TYPE_IOSTREAM_NUMERIC:
        case TIS_IO_TYPE_IGENERATOR_ALGEBRAIC:
        case TIS_IO_TYPE_IGENERATOR_CONSTANT:
        case TIS_IO_TYPE_IGENERATOR_CYCLIC:
        case TIS_IO_TYPE_IGENERATOR_RANDOM:
        case TIS_IO_TYPE_IGENERATOR_SEQUENCE:
        default:
            error("Not yet implemented\n");
            return TIS_OP_RESULT_ERR;
    }
    return TIS_OP_RESULT_OK;
}

tis_op_result_t output(tis_io_node_t* io, int value) {
    if(io == NULL) {
        return TIS_OP_RESULT_WRITE_WAIT;
    }
    int out = EOF;
    switch(io->type) {
        case TIS_IO_TYPE_IOSTREAM_ASCII:
            out = fputc(value, io->file);
            if(out == EOF) {
                //return TIS_OP_RESULT_READ_WAIT;
            }
            break;
        case TIS_IO_TYPE_IOSTREAM_NUMERIC:
        case TIS_IO_TYPE_OSTREAM_IMAGE:
        default:
            error("Not yet implemented\n");
            return TIS_OP_RESULT_ERR;
    }
    return TIS_OP_RESULT_OK;
}


#include <stdio.h>

#include "tis_types.h"

tis_op_result_t input(tis_io_node_t* io, int* value) {
    if(io == NULL) {
        return TIS_OP_RESULT_READ_WAIT;
    }
    int in = EOF;
    switch(io->type) {
        case TIS_IO_TYPE_IOSTREAM_ASCII:
            if((in = fgetc(io->file.file)) == EOF) {
                return TIS_OP_RESULT_READ_WAIT;
            }
            *value = clamp(in);
            break;
        case TIS_IO_TYPE_IOSTREAM_NUMERIC:
            if(fscanf(io->file.file, " %d ", &in) != 1) {
                return TIS_OP_RESULT_READ_WAIT;
            }
            *value = clamp(in);
            break;
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

#define TIS_NUMERIC_SEP " " // separator to use when printing in NUMERIC mode
tis_op_result_t output(tis_io_node_t* io, int value) {
    if(io == NULL) {
        return TIS_OP_RESULT_WRITE_WAIT;
    }
    int out = EOF;
    switch(io->type) {
        case TIS_IO_TYPE_IOSTREAM_ASCII:
            if((out = fputc(value, io->file.file)) == EOF) {
                //return TIS_OP_RESULT_WRITE_WAIT; // TODO what should I do here?
            }
            break;
        case TIS_IO_TYPE_IOSTREAM_NUMERIC:
            if(io->file.sep >= 0) {
                if(fprintf(io->file.file, "%d%c", value, io->file.sep) > 0) {
                    //return TIS_OP_RESULT_WRITE_WAIT; // TODO what should I do here?
                }
            } else {
                if(fprintf(io->file.file, "%d", value) > 0) {
                    //return TIS_OP_RESULT_WRITE_WAIT; // TODO what should I do here?
                }
            }
            break;
        case TIS_IO_TYPE_OSTREAM_IMAGE:
        default:
            error("Not yet implemented\n");
            return TIS_OP_RESULT_ERR;
    }
    return TIS_OP_RESULT_OK;
}

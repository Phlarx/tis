
#include <stdio.h>

#include "tis_types.h"

// These are duplicated from the header
tis_op_result_t input(tis_io_node_t* io, int* value);
tis_op_result_t output(tis_io_node_t* io, int value);

tis_node_state_t run_input(tis_t* tis, tis_io_node_t* io) {
    (void)tis;
    if(io == NULL) {
        return TIS_NODE_STATE_IDLE;
    }
    if(io->writereg != TIS_REGISTER_INVALID) {
        // still waiting for current write
        return TIS_NODE_STATE_WRITE_WAIT;
    }
    spam("Input node I%zu attempting to write\n", io->col);
    tis_op_result_t result = input(io, &(io->writebuf));
    if(result == TIS_OP_RESULT_OK) {
        return TIS_NODE_STATE_WRITE_WAIT;
    } else if(result == TIS_OP_RESULT_READ_WAIT) {
        return TIS_NODE_STATE_READ_WAIT;
    } else {
        // BAD INTERNAL ERROR BAD this is out of sync with the enum
        error("INTERNAL: An error has occurred!!!\n");
        bork();
    }
}

tis_node_state_t run_output(tis_t* tis, tis_io_node_t* io) {
    if(io == NULL) {
        return TIS_NODE_STATE_IDLE;
    }
    spam("Output node O%zu attempting to read\n", io->col);
    tis_op_result_t result;
    if(tis->rows == 0) { // if reading up with no rows, read input instead
        if(tis->inputs[io->col] == NULL || tis->inputs[io->col]->writereg != TIS_REGISTER_DOWN) {
            return TIS_NODE_STATE_READ_WAIT;
        }
        result = output(io, tis->inputs[io->col]->writebuf);
        tis->inputs[io->col]->writereg = TIS_REGISTER_NIL;
        if(result == TIS_OP_RESULT_OK) {
            spam("Output node O%zu read success\n", io->col);
            return TIS_NODE_STATE_RUNNING;
        } else {
            // BAD INTERNAL ERROR BAD this is out of sync with the enum
            error("INTERNAL: An error has occurred!!!\n");
            bork();
        }
    }
    tis_node_t* neigh = tis->nodes[(tis->rows-1)*tis->cols + io->col];
    if(neigh == NULL || !(neigh->writereg == TIS_REGISTER_DOWN || neigh->writereg == TIS_REGISTER_ANY)) {
        return TIS_NODE_STATE_READ_WAIT;
    }
    result = output(io, neigh->writebuf);
    if(neigh->writereg == TIS_REGISTER_ANY) {
        neigh->last = TIS_REGISTER_DOWN;
    }
    neigh->writereg = TIS_REGISTER_NIL;
    if(result == TIS_OP_RESULT_OK) {
        spam("Output node O%zu read success\n", io->col);
        return TIS_NODE_STATE_RUNNING;
    } else {
        // BAD INTERNAL ERROR BAD this is out of sync with the enum
        error("INTERNAL: An error has occurred!!!\n");
        bork();
    }
}

tis_node_state_t run_input_defer(tis_t* tis, tis_io_node_t* io) {
    (void)tis;
    spam("Input node I%zu attempting to write (defer)\n", io->col);
    if(1 /* TODO is input */ ) {
        if(io->writereg == TIS_REGISTER_NIL) { // if NIL, the previous write was handled, reset it all
            io->writereg = TIS_REGISTER_INVALID;
            spam("Input node I%zu write deferred success\n", io->col);
            return TIS_NODE_STATE_RUNNING;
        } else {
            io->writereg = TIS_REGISTER_DOWN;
            return TIS_NODE_STATE_WRITE_WAIT;
        }
    } else {
        // TODO internal error
        bork();
    }
}

tis_node_state_t run_output_defer(tis_t* tis, tis_io_node_t* io) {
    (void)tis;
    (void)io;
    // TODO internal error
    bork();
}

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
        case TIS_IO_TYPE_IGENERATOR_LIST:
        case TIS_IO_TYPE_IGENERATOR_CYCLIC:
        case TIS_IO_TYPE_IGENERATOR_RANDOM:
        case TIS_IO_TYPE_IGENERATOR_ALGEBRAIC:
        case TIS_IO_TYPE_IGENERATOR_GEOMETRIC:
        case TIS_IO_TYPE_IGENERATOR_HARMONIC:
        case TIS_IO_TYPE_IGENERATOR_OEIS:
        default:
            error("Not yet implemented\n");
            return TIS_OP_RESULT_ERR;
    }
    return TIS_OP_RESULT_OK;
}

#define TIS_NUMERIC_SEP " " // separator to use when printing in NUMERIC mode
tis_op_result_t output(tis_io_node_t* io, int value) {
    int out = EOF;
    switch(io->type) {
        case TIS_IO_TYPE_IOSTREAM_ASCII:
            if(io->file.file != NULL) {
                if((out = fputc(value, io->file.file)) == EOF) {
                    error("An error occurred when writing value %d to file, silently dropping future values\n", value);
                    io->file.file = NULL; // this file handle is still closeable by the normal method
                }
            } else {
                spam("Output silently dropping value %d\n", value);
            }
            break;
        case TIS_IO_TYPE_IOSTREAM_NUMERIC:
            if(io->file.file != NULL) {
                if(io->file.sep >= 0) {
                    if(fprintf(io->file.file, "%d%c", value, io->file.sep) < 0) {
                        error("An error occurred when writing value %d to file, silently dropping future values\n", value);
                        io->file.file = NULL; // this file handle is still closeable by the normal method
                    }
                } else {
                    if(fprintf(io->file.file, "%d", value) < 0) {
                        error("An error occurred when writing value %d to file, silently dropping future values\n", value);
                        io->file.file = NULL; // this file handle is still closeable by the normal method
                    }
                }
            } else {
                spam("Output silently dropping value %d\n", value);
            }
            break;
        case TIS_IO_TYPE_OSTREAM_IMAGE:
        default:
            error("Not yet implemented\n");
            return TIS_OP_RESULT_ERR;
    }
    return TIS_OP_RESULT_OK;
}

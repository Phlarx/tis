
#include <stdio.h>
#include <string.h>

#include "tis_io.h"
#include "tis_node.h"
#include "tis_ops.h"
#include "tis_types.h"

tis_node_state_t run(tis_t* tis, tis_node_t* node) {
    if(node->type == TIS_NODE_TYPE_COMPUTE) {
        int start_index = node->index;
        while(node->code[node->index] == NULL || node->code[node->index]->type == TIS_OP_TYPE_INVALID) {
            node->index = (node->index + 1) % TIS_NODE_LINE_COUNT;
            if(node->index == start_index) {
                return TIS_NODE_STATE_IDLE;
            }
        }

        tis_op_result_t result = step(tis, node, node->code[node->index]);
        if(result == TIS_OP_RESULT_OK) {
            node->index = (node->index + 1) % TIS_NODE_LINE_COUNT;
            return TIS_NODE_STATE_RUNNING;
        } else if(result == TIS_OP_RESULT_READ_WAIT) {
            return TIS_NODE_STATE_READ_WAIT;
        } else if(result == TIS_OP_RESULT_WRITE_WAIT) {
            return TIS_NODE_STATE_WRITE_WAIT;
        } else if(result == TIS_OP_RESULT_ERR) {
            error("An error has occurred!!!\n");
            bork();
        } else {
            // BAD INTERNAL ERROR BAD this is out of sync with the enum
            error("INTERNAL: An error has occurred!!!\n");
            bork();
        }
    } else if(node->type == TIS_NODE_TYPE_MEMORY_STACK) {
        // TODO experiment: can a stack node handle simultaneous read and write? What does this do, even? Should read or write be first?
        tis_node_state_t state = TIS_NODE_STATE_IDLE;
        if(node->index < TIS_NODE_LINE_COUNT) {
            // if capacity, try to read
            spam("Stack node %s attempting to read to index %d\n", node_name(node), node->index);
            if(read_register(tis, node, TIS_REGISTER_ANY, &(node->data[node->index])) == TIS_OP_RESULT_OK) {
                spam("Stack node %s read success to index %d\n", node_name(node), node->index);
                node->index++;
                state = TIS_NODE_STATE_RUNNING;
            }
        }
        if(node->index > 0) {
            spam("Stack node %s attempting to write from index %d\n", node_name(node), node->index-1);
            tis_op_result_t result = write_register(tis, node, TIS_REGISTER_ANY, node->data[node->index-1]);
            if(result == TIS_OP_RESULT_OK) {
                spam("Stack node %s write immediate success from index %d\n", node_name(node), node->index-1);
                node->index--;
                state = TIS_NODE_STATE_RUNNING;
            } else {
                state = TIS_OP_RESULT_WRITE_WAIT;
            }
        }
        return state;
    } else if(node->type == TIS_NODE_TYPE_DAMAGED) {
        return TIS_NODE_STATE_IDLE;
    }
    return TIS_NODE_STATE_IDLE;
}

tis_node_state_t run_defer(tis_t* tis, tis_node_t* node) {
    if(node->type == TIS_NODE_TYPE_COMPUTE) {
        tis_op_result_t result = step_defer(tis, node, node->code[node->index]);
        if(result == TIS_OP_RESULT_OK) {
            node->index = (node->index + 1) % TIS_NODE_LINE_COUNT;
            return TIS_NODE_STATE_RUNNING;
        } else if(result == TIS_OP_RESULT_READ_WAIT) {
            // internal error
            bork();
        } else if(result == TIS_OP_RESULT_WRITE_WAIT) {
            return TIS_NODE_STATE_WRITE_WAIT;
        } else if(result == TIS_OP_RESULT_ERR) {
            error("An error has occurred!!!\n");
            bork();
        } else {
            // BAD INTERNAL ERROR BAD this is out of sync with the enum
            error("INTERNAL: An error has occurred!!!\n");
            bork();
        }
    } else if(node->type == TIS_NODE_TYPE_MEMORY_STACK) {
        spam("Stack node %s attempting to write (defer) from index %d\n", node_name(node), node->index-1);
        tis_op_result_t result = write_register_defer(tis, node, TIS_REGISTER_ANY);
        if(result == TIS_OP_RESULT_OK) {
            spam("Stack node %s write deferred success from index %d\n", node_name(node), node->index-1);
            node->index--;
            return TIS_NODE_STATE_RUNNING;
        } else if(result == TIS_OP_RESULT_READ_WAIT) {
            // internal error
            bork();
        } else if(result == TIS_OP_RESULT_WRITE_WAIT) {
            return TIS_NODE_STATE_WRITE_WAIT;
        } else if(result == TIS_OP_RESULT_ERR) {
            error("An error has occurred!!!\n");
            bork();
        } else {
            // BAD INTERNAL ERROR BAD this is out of sync with the enum
            error("INTERNAL: An error has occurred!!!\n");
            bork();
        }
    } else {
        // only compute and memory nodes can defer
        error("INTERNAL: Cannot run deferred instructions on this node type\n");
        bork();
    }
    return TIS_NODE_STATE_IDLE;
}

/*
 * In game, ANY search order for source is LEFT, RIGHT, UP, DOWN
 * This is not in the spec, but is convenient and will be maintained
 * TODO future enhancement to randomly order, giving a source of randomness
 */
tis_op_result_t read_port_register_maybe(tis_t* tis, tis_node_t* node, tis_register_t reg, int* value) {
    if(reg == TIS_REGISTER_ANY) {
        tis_op_result_t result;
        result = read_port_register_maybe(tis, node, TIS_REGISTER_LEFT, value);
        if(result == TIS_OP_RESULT_OK) {
            node->last = TIS_REGISTER_LEFT;
            return result;
        }
        result = read_port_register_maybe(tis, node, TIS_REGISTER_RIGHT, value);
        if(result == TIS_OP_RESULT_OK) {
            node->last = TIS_REGISTER_RIGHT;
            return result;
        }
        result = read_port_register_maybe(tis, node, TIS_REGISTER_UP, value);
        if(result == TIS_OP_RESULT_OK) {
            node->last = TIS_REGISTER_UP;
            return result;
        }
        result = read_port_register_maybe(tis, node, TIS_REGISTER_DOWN, value);
        if(result == TIS_OP_RESULT_OK) {
            node->last = TIS_REGISTER_DOWN;
            return result;
        }
        return TIS_OP_RESULT_READ_WAIT;
    } else if(reg == TIS_REGISTER_UP) {
        if(node->row == 0) { // if reading up from top row, read input instead
            return input(tis->inputs[node->col], value); // TODO experiment: can io read happen every tick, or only every other?
        }
        tis_node_t* neigh = tis->nodes[(node->row-1)*tis->cols + node->col];
        if(neigh == NULL || !(neigh->writereg == TIS_REGISTER_DOWN || neigh->writereg == TIS_REGISTER_ANY)) {
            return TIS_OP_RESULT_READ_WAIT;
        }
        *value = neigh->writebuf;
        if(neigh->writereg == TIS_REGISTER_ANY) {
            neigh->last = TIS_REGISTER_DOWN;
        }
        neigh->writereg = TIS_REGISTER_NIL;
    } else if(reg == TIS_REGISTER_DOWN) {
        if(node->row+1 == tis->rows) { // can never read from an output
            return TIS_OP_RESULT_READ_WAIT;
        }
        tis_node_t* neigh = tis->nodes[(node->row+1)*tis->cols + node->col];
        if(neigh == NULL || !(neigh->writereg == TIS_REGISTER_UP || neigh->writereg == TIS_REGISTER_ANY)) {
            return TIS_OP_RESULT_READ_WAIT;
        }
        *value = neigh->writebuf;
        if(neigh->writereg == TIS_REGISTER_ANY) {
            neigh->last = TIS_REGISTER_UP;
        }
        neigh->writereg = TIS_REGISTER_NIL;
    } else if(reg == TIS_REGISTER_LEFT) {
        if(node->col == 0) {
            return TIS_OP_RESULT_READ_WAIT;
        }
        tis_node_t* neigh = tis->nodes[node->row*tis->cols + node->col-1];
        if(neigh == NULL || !(neigh->writereg == TIS_REGISTER_RIGHT || neigh->writereg == TIS_REGISTER_ANY)) {
            return TIS_OP_RESULT_READ_WAIT;
        }
        *value = neigh->writebuf;
        if(neigh->writereg == TIS_REGISTER_ANY) {
            neigh->last = TIS_REGISTER_RIGHT;
        }
        neigh->writereg = TIS_REGISTER_NIL;
    } else if(reg == TIS_REGISTER_RIGHT) {
        if(node->col+1 == tis->cols) {
            return TIS_OP_RESULT_READ_WAIT;
        }
        tis_node_t* neigh = tis->nodes[node->row*tis->cols + node->col+1];
        if(neigh == NULL || !(neigh->writereg == TIS_REGISTER_LEFT || neigh->writereg == TIS_REGISTER_ANY)) {
            return TIS_OP_RESULT_READ_WAIT;
        }
        *value = neigh->writebuf;
        if(neigh->writereg == TIS_REGISTER_ANY) {
            neigh->last = TIS_REGISTER_LEFT;
        }
        neigh->writereg = TIS_REGISTER_NIL;
    }
    return TIS_OP_RESULT_OK;
}

/*
 * In game, ANY search order for destination is UP, LEFT, RIGHT, DOWN
 * This is not in the spec, but is convenient and will be maintained
 * Thus, the winner is determined by the node run order (and not here)
 * TODO future enhancement to randomly order, giving a source of randomness
 */
tis_op_result_t write_port_register_maybe(tis_t* tis, tis_node_t* node, tis_register_t reg, int value) {
    node->writebuf = value;
    // if writing down from bottom row, write output instead, and return OK
    if((reg == TIS_REGISTER_DOWN || reg == TIS_REGISTER_ANY) && node->row+1 == tis->rows) {
        spam("Write value %d to output index %zu\n", value, node->col);
        return output(tis->outputs[node->col], value);
    }
    // TODO experiment: does writing to output claim 1 or 2 ticks? (move to other claims 2)
    // TODO experiment: does writing to ANY favor outputs or other nodes?
    return TIS_OP_RESULT_WRITE_WAIT;
}
tis_op_result_t write_port_register_defer_maybe(tis_t* tis, tis_node_t* node, tis_register_t reg) {
    (void)tis;
    if(node->writereg == TIS_REGISTER_NIL) { // if NIL, the previous write was handled, reset it all
        node->writereg = TIS_REGISTER_INVALID;
        return TIS_OP_RESULT_OK;
    }
    node->writereg = reg;
    return TIS_OP_RESULT_WRITE_WAIT;
}

tis_op_result_t read_register(tis_t* tis, tis_node_t* node, tis_register_t reg, int* value) {
    spam("Attempting read from register %s on node %s\n", reg_to_string(reg), node_name(node));
    switch(reg) {
        case TIS_REGISTER_ACC:
            *value = node->acc;
            return TIS_OP_RESULT_OK;
        case TIS_REGISTER_BAK:
            // cannot read from BAK
            error("INTERNAL: Attempted to read from BAK on node %s\n", node_name(node));
            return TIS_OP_RESULT_ERR;
        case TIS_REGISTER_NIL:
            *value = 0;
            return TIS_OP_RESULT_OK;
        case TIS_REGISTER_UP:
        case TIS_REGISTER_DOWN:
        case TIS_REGISTER_LEFT:
        case TIS_REGISTER_RIGHT:
        case TIS_REGISTER_ANY:
            return read_port_register_maybe(tis, node, reg, value);
        case TIS_REGISTER_LAST:
            if(node->last == TIS_REGISTER_INVALID) {
                error("Attempted to reference LAST before ANY on node %s\n", node_name(node));
                return TIS_OP_RESULT_ERR;
            }
            return read_port_register_maybe(tis, node, node->last, value);
        case TIS_REGISTER_INVALID:
        default:
            // internal error
            error("INTERNAL: Attempted to read from INVALID on node %s\n", node_name(node));
            return TIS_OP_RESULT_ERR;
    }
    // Should not reach
    error("INTERNAL: switch out of sync with enum\n");
    return TIS_OP_RESULT_ERR;
}

tis_op_result_t write_register(tis_t* tis, tis_node_t* node, tis_register_t reg, int value) {
    spam("Attempting write to register %s on node %s (value %d)\n", reg_to_string(reg), node_name(node), value);
    switch(reg) {
        case TIS_REGISTER_ACC:
            node->acc = value;
            return TIS_OP_RESULT_OK;
        case TIS_REGISTER_BAK:
            // cannot write to BAK
            error("INTERNAL: Attempted to write to BAK on node %s\n", node_name(node));
            return TIS_OP_RESULT_ERR;
        case TIS_REGISTER_NIL:
            // throwaway write
            return TIS_OP_RESULT_OK;
        case TIS_REGISTER_UP:
        case TIS_REGISTER_DOWN:
        case TIS_REGISTER_LEFT:
        case TIS_REGISTER_RIGHT:
        case TIS_REGISTER_ANY:
            return write_port_register_maybe(tis, node, reg, value);
        case TIS_REGISTER_LAST:
            if(node->last == TIS_REGISTER_INVALID) {
                error("Attempted to reference LAST before ANY on node %s\n", node_name(node));
                return TIS_OP_RESULT_ERR;
            }
            return write_port_register_maybe(tis, node, node->last, value);
        case TIS_REGISTER_INVALID:
        default:
            // internal error
            error("INTERNAL: Attempted to write to INVALID on node %s\n", node_name(node));
            return TIS_OP_RESULT_ERR;
    }
    // Should not reach
    error("INTERNAL: switch out of sync with enum\n");
    return TIS_OP_RESULT_ERR;
}

tis_op_result_t write_register_defer(tis_t* tis, tis_node_t* node, tis_register_t reg) {
    spam("Attempting write to register %s on node %s (defer)\n", reg_to_string(reg), node_name(node));
    switch(reg) {
        case TIS_REGISTER_ACC:
        case TIS_REGISTER_BAK:
        case TIS_REGISTER_NIL:
            // internal error
            error("INTERNAL: Attempted to write (defer) to ACC, NIL, or BAK on node %s\n", node_name(node));
            return TIS_OP_RESULT_ERR;
        case TIS_REGISTER_UP:
        case TIS_REGISTER_DOWN:
        case TIS_REGISTER_LEFT:
        case TIS_REGISTER_RIGHT:
        case TIS_REGISTER_ANY:
            return write_port_register_defer_maybe(tis, node, reg);
        case TIS_REGISTER_LAST:
            if(node->last == TIS_REGISTER_INVALID) {
                error("INTERNAL: Attempted to reference LAST before ANY on node %s (this should already have been caught)\n", node_name(node));
                return TIS_OP_RESULT_ERR;
            }
            return write_port_register_defer_maybe(tis, node, node->last);
        case TIS_REGISTER_INVALID:
        default:
            // internal error
            error("INTERNAL: Attempted to write (defer) to INVALID on node %s\n", node_name(node));
            return TIS_OP_RESULT_ERR;
    }
    // Should not reach
    error("INTERNAL: switch out of sync with enum\n");
    return TIS_OP_RESULT_ERR;
}

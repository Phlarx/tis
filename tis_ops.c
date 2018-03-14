
#include <stdio.h>
#include <string.h>

#include "tis_types.h"
#include "tis_node.h"

tis_op_result_t step(tis_t* tis, tis_node_t* node, tis_op_t* op) {
    if(node->type == TIS_NODE_TYPE_COMPUTE) {
        tis_op_result_t result = TIS_OP_RESULT_OK;
        char* jump = NULL;
        int value = 0;
        spam("Run instruction %s on node %s\n", op_to_string(op->type), node_name(node));
        // TODO assert correct nargs? This is checked when parsing though...
        switch(op->type) {
            case TIS_OP_TYPE_ADD:
                if(op->src.type == TIS_OP_ARG_TYPE_CONSTANT) {
                    node->acc = clamp(node->acc + op->src.con);
                } else if(op->src.type == TIS_OP_ARG_TYPE_REGISTER) {
                    result = read_register(tis, node, op->src.reg, &value);
                    if(result == TIS_OP_RESULT_OK) {
                        node->acc = clamp(node->acc + value);
                    }
                } else {
                    error("INTERNAL: Invalid arg type for ADD (%d) on node %s\n", op->src.type, node_name(node));
                    result = TIS_OP_RESULT_ERR;
                }
                break;
            case TIS_OP_TYPE_HCF:
                halt();
                break;
            case TIS_OP_TYPE_JEZ:
                if(node->acc == 0) {
                    goto jump_label;
                }
                break;
            case TIS_OP_TYPE_JGZ:
                if(node->acc > 0) {
                    goto jump_label;
                }
                break;
            case TIS_OP_TYPE_JLZ:
                if(node->acc < 0) {
                    goto jump_label;
                }
                break;
            case TIS_OP_TYPE_JMP:
jump_label:
                if(op->src.type == TIS_OP_ARG_TYPE_LABEL) {
                    jump = op->src.label;
                } else {
                    error("INTERNAL: Unable to jump to non-label argument on node %s\n", node_name(node));
                    result = TIS_OP_RESULT_ERR;
                }
                break;
            case TIS_OP_TYPE_JNZ:
                if(node->acc != 0) {
                    goto jump_label;
                }
                break;
            case TIS_OP_TYPE_JRO:
                if(op->src.type == TIS_OP_ARG_TYPE_CONSTANT) {
                    node->index = (node->index + op->src.con) % TIS_NODE_LINE_COUNT;
                } else if(op->src.type == TIS_OP_ARG_TYPE_REGISTER) {
                    result = read_register(tis, node, op->src.reg, &value);
                    if(result == TIS_OP_RESULT_OK) {
                        node->index = (node->index + value) % TIS_NODE_LINE_COUNT;
                    }
                } else {
                    error("INTERNAL: Invalid arg type for JRO (%d) on node %s\n", op->src.type, node_name(node));
                    result = TIS_OP_RESULT_ERR;
                }
                break;
            case TIS_OP_TYPE_MOV:
                if(node->writereg != TIS_REGISTER_INVALID) {
                    // still waiting for current write
                    result = TIS_OP_RESULT_WRITE_WAIT;
                    break;
                }
                if(op->src.type == TIS_OP_ARG_TYPE_CONSTANT) {
                    value = op->src.con;
                } else if(op->src.type == TIS_OP_ARG_TYPE_REGISTER) {
                    result = read_register(tis, node, op->src.reg, &value);
                    if(result != TIS_OP_RESULT_OK) {
                        break;
                    }
                } else {
                    error("INTERNAL: Invalid source arg type for MOV (%d) on node %s\n", op->src.type, node_name(node));
                    result = TIS_OP_RESULT_ERR;
                }
                if(op->dst.type == TIS_OP_ARG_TYPE_REGISTER) {
                    result = write_register(tis, node, op->dst.reg, value);
                } else {
                    error("INTERNAL: Invalid dest arg type for MOV (%d) on node %s\n", op->dst.type, node_name(node));
                    result = TIS_OP_RESULT_ERR;
                }
                break;
            case TIS_OP_TYPE_NEG:
                node->acc = -node->acc;
                break;
            case TIS_OP_TYPE_NOP:
                // do nothing! (actually does "ADD 0" under the hood in real TIS)
                // node->acc += 0;
                break;
            case TIS_OP_TYPE_SAV:
                node->bak = node->acc;
                break;
            case TIS_OP_TYPE_SUB:
                if(op->src.type == TIS_OP_ARG_TYPE_CONSTANT) {
                    node->acc = clamp(node->acc - op->src.con);
                } else if(op->src.type == TIS_OP_ARG_TYPE_REGISTER) {
                    result = read_register(tis, node, op->src.reg, &value);
                    if(result == TIS_OP_RESULT_OK) {
                        node->acc = clamp(node->acc - value);
                    }
                } else {
                    error("INTERNAL: Invalid arg type for SUB (%d) on node %s\n", op->src.type, node_name(node));
                    result = TIS_OP_RESULT_ERR;
                }
                break;
            case TIS_OP_TYPE_SWP:
                value = node->bak;
                node->bak = node->acc;
                node->acc = value;
                break;
            case TIS_OP_TYPE_INVALID:
            default:
                error("Attempted to run an inavlid instruction on node %s\n", node_name(node));
                result = TIS_OP_RESULT_ERR;
                break;
        }
        if(jump != NULL) {
            spam("Jumping to label %.20s on node %s\n", jump, node_name(node));
            int i = 0;
            for(; i < TIS_NODE_LINE_COUNT; i++) {
                if(node->code[i]->label != NULL && strcmp(jump, node->code[i]->label) == 0) {
                    node->index = i-1; // jump to instuction *before* label
                    break;
                }
            }
            if(i == TIS_NODE_LINE_COUNT) {
                // unable to jump to missing label
                error("Label %.20s not found in node %s, unable to jump\n", jump, node_name(node));
                result = TIS_OP_RESULT_ERR;
            }
        }
        spam("Run instruction %s on node %s result %s\n", op_to_string(op->type), node_name(node), result_to_string(result));
        return result;
    } else {
        error("INTERNAL: Cannot run instructions on this node type\n");
        return TIS_OP_RESULT_ERR;
    }
}

tis_op_result_t step_defer(tis_t* tis, tis_node_t* node, tis_op_t* op) {
    if(node->type == TIS_NODE_TYPE_COMPUTE) {
        // This should only be called when deferring a write to an external port
        // The only op that can do that is MOV
        tis_op_result_t result;
        spam("Run instruction %s on node %s (defer)\n", op_to_string(op->type), node_name(node));
        if(op->type != TIS_OP_TYPE_MOV) {
            error("INTERNAL: Only MOV instructions may be deferred; node %s\n", node_name(node));
            result = TIS_OP_RESULT_ERR;
        } else {
            if(op->dst.type == TIS_OP_ARG_TYPE_REGISTER) {
                result = write_register_defer(tis, node, op->dst.reg);
            } else {
                error("INTERNAL: Invalid dest arg type for MOV (%d) on node %s\n", op->dst.type, node_name(node));
                result = TIS_OP_RESULT_ERR;
            }
        }
        spam("Run instruction %s on node %s (defer) result %s\n", op_to_string(op->type), node_name(node), result_to_string(result));
        return result;
    } else {
        error("INTERNAL: Cannot run deferred instructions on this node type\n");
        return TIS_OP_RESULT_ERR;
    }
}

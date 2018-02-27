#define _POSIX_C_SOURCE 200809L // for strdup()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "tis_types.h"
#include "tis_node.h"

#define INIT_OK 0
#define INIT_FAIL 1

int init_layout(tis_t* tis, char* layoutfile) {
    FILE* layout = NULL;
    if(layoutfile != NULL) {
        layout = fopen(layoutfile, "r");
    }
    if(layout != NULL) {
        // set size from file
        // TODO
        tis->rows = 3;
        tis->cols = 4;
    }

    if(tis->cols == 0) {
        if(layout != NULL) {
            fclose(layout);
        }
        error("Cannot initialize with zero columns\n"); // But zero rows are fine, right? TODO
        return INIT_FAIL;
    }

    tis->size = tis->rows*tis->cols;
    tis->nodes = calloc(tis->size, sizeof(tis_node_t*));
    tis->inputs = calloc(tis->cols, sizeof(tis_io_node_t*));
    tis->outputs = calloc(tis->cols, sizeof(tis_io_node_t*));

    if(layout != NULL) {
        // init node & io node layout from file
        // TODO
        fclose(layout);
    } else {
        // init default node & io node layout for dimensions
        // set all nodes to TIS_NODE_TYPE_COMPUTE
        for(size_t i = 0; i < tis->size; i++) {
            tis->nodes[i] = calloc(1, sizeof(tis_node_t));
            tis->nodes[i]->type = TIS_NODE_TYPE_COMPUTE;
            tis->nodes[i]->id = i;
            tis->nodes[i]->row = i / tis->cols;
            tis->nodes[i]->col = i % tis->cols;
            tis->nodes[i]->writereg = TIS_REGISTER_INVALID;
            tis->nodes[i]->last = TIS_REGISTER_INVALID;
        }
        // set first input to TIS_IO_TYPE_IOSTREAM_NUMERIC
        tis->inputs[0] = calloc(1, sizeof(tis_io_node_t));
        tis->inputs[0]->type = TIS_IO_TYPE_IOSTREAM_ASCII;
        // set last output to TIS_IO_TYPE_IOSTREAM_NUMERIC
        tis->outputs[tis->cols - 1] = calloc(1, sizeof(tis_io_node_t));
        tis->outputs[tis->cols - 1]->type = TIS_IO_TYPE_IOSTREAM_ASCII;
    }

    return INIT_OK;
}

int init_nodes(tis_t* tis, char* sourcefile) {
    FILE* source = fopen(sourcefile, "r");

    int BUFSIZE = 100;
    //int NODELINELEN = 19;  // TODO is this the correct max line length?
    char buf[BUFSIZE];
    int id = -1;
    int line = 0;
    tis_node_t* node = NULL;
    while(fgets(buf, BUFSIZE, source) != NULL) {
        char* nl = strchr(buf, '\n');
        if(nl == NULL) {
            error("Line too long:\n");
            error("    %.*s\n", BUFSIZE, buf);
        } else {
            *nl = '\0';
        }

        debug("parse line:  %.*s\n", BUFSIZE, buf);

        if(buf[0] == '\0') {
            // empty line; ignore (TODO enforce empty line before new node?)
        } else if(sscanf(buf, "@%d", &id) == 1) { // TODO check for extra data on this line
            // start new node (TODO this allows out-of-order. warn?)
            node = NULL;
            for(size_t i = 0; i < tis->size; i++) {
                if(tis->nodes[i]->id == id) {
                    node = tis->nodes[i];
                    line = -1; // will be zero next line
                    break;
                }
            }
            if(node == NULL) {
                warn("@%d is out-of-bounds for the current layout. Contents will be ignored.\n", id);
            }
        } else if(node != NULL && line < 15) { // TODO warn on overlength lines (len > NODELINELEN)
            node->code[line] = calloc(1, sizeof(tis_op_t));
            node->code[line]->linenum = line+1; // these are 1-indexed
            node->code[line]->linetext = strdup(buf);

            // TODO parse breakpoints (!) (possible future enhancement)

            char* temp = NULL;
            char* temp2 = NULL;
            int val = 0;
            int nargs = 0;
            if(tis->name == NULL) {
                if((temp = strstr(buf, "##")) != NULL) {
                    temp += 2; // skip past ##
                    tis->name = strdup(temp); // TODO strip whitespace
                }
            }
            if((temp = strchr(buf, '#')) != NULL) {
                *temp = '\0';
            }
            if((temp = strchr(buf, ':')) != NULL) {
                *temp = '\0';
                temp++; // temp now points just after label
                node->code[line]->label = strdup(buf); // TODO strip whitespace (but this would be invalid for real TIS), verify label is A-Z0-9~`$%^&*()_-+={}[]|\;"'<>,.?/,
            } else {                                   // labels may be 18 chars (whole line) but longest useful is 14 (for jmp <label>)??? off by one???
                temp = buf;
            }

            temp = strtok(temp, " ");
            if(temp == NULL) {
                node->code[line]->type = TIS_OP_TYPE_INVALID; // line contains no code
                nargs = 0;
            } else if(strcasecmp(temp, "ADD") == 0) {
                node->code[line]->type = TIS_OP_TYPE_ADD;
                nargs = 1;
            } else if(strcasecmp(temp, "HCF") == 0) {
                node->code[line]->type = TIS_OP_TYPE_HCF;
                nargs = 0;
            } else if(strcasecmp(temp, "JEZ") == 0) {
                node->code[line]->type = TIS_OP_TYPE_JEZ;
                nargs = 1;
                val = 1; // set value to non-zero as a flag that this is a jump op that uses a label arg (JRO doesn't qualify)
            } else if(strcasecmp(temp, "JGZ") == 0) {
                node->code[line]->type = TIS_OP_TYPE_JGZ;
                nargs = 1;
                val = 1;
            } else if(strcasecmp(temp, "JLZ") == 0) {
                node->code[line]->type = TIS_OP_TYPE_JLZ;
                nargs = 1;
                val = 1;
            } else if(strcasecmp(temp, "JMP") == 0) {
                node->code[line]->type = TIS_OP_TYPE_JMP;
                nargs = 1;
                val = 1;
            } else if(strcasecmp(temp, "JNZ") == 0) {
                node->code[line]->type = TIS_OP_TYPE_JNZ;
                nargs = 1;
                val = 1;
            } else if(strcasecmp(temp, "JRO") == 0) {
                node->code[line]->type = TIS_OP_TYPE_JRO;
                nargs = 1;
            } else if(strcasecmp(temp, "MOV") == 0) {
                node->code[line]->type = TIS_OP_TYPE_MOV;
                nargs = 2;
            } else if(strcasecmp(temp, "NEG") == 0) {
                node->code[line]->type = TIS_OP_TYPE_NEG;
                nargs = 0;
            } else if(strcasecmp(temp, "NOP") == 0) {
                node->code[line]->type = TIS_OP_TYPE_NOP;
                nargs = 0;
            } else if(strcasecmp(temp, "SAV") == 0) {
                node->code[line]->type = TIS_OP_TYPE_SAV;
                nargs = 0;
            } else if(strcasecmp(temp, "SUB") == 0) {
                node->code[line]->type = TIS_OP_TYPE_SUB;
                nargs = 1;
            } else if(strcasecmp(temp, "SWP") == 0) {
                node->code[line]->type = TIS_OP_TYPE_SWP;
                nargs = 0;
            } else {
                node->code[line]->type = TIS_OP_TYPE_INVALID; // TODO give warning; operator unrecognized
                nargs = 0;
            }
            if(nargs > 0) {
                temp = strtok(NULL, " ,");
                // TODO validate args for op type
                if(temp == NULL) {
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_NONE;
                } else if(val == 1) { // labels are only valid as sources (...syntactically. semantically, they are a dst; syntactically, they are actually a src)
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_LABEL; // TODO are these register keywords valid as labels? are numerics valid as labels?
                    node->code[line]->src.label = strdup(temp); // whitespace is already stripped by strtok
                } else if((val = strtol(temp, &temp2, 0), temp != temp2 && *temp2 == '\0')) { // constants are only valid as sources
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_CONSTANT;
                    node->code[line]->src.con = val;
                } else if(strcasecmp(temp, "ACC") == 0) {
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->src.reg = TIS_REGISTER_ACC;
                } else if(strcasecmp(temp, "NIL") == 0) {
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->src.reg = TIS_REGISTER_NIL;
                } else if(strcasecmp(temp, "UP") == 0) {
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->src.reg = TIS_REGISTER_UP;
                } else if(strcasecmp(temp, "DOWN") == 0) {
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->src.reg = TIS_REGISTER_DOWN;
                } else if(strcasecmp(temp, "LEFT") == 0) {
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->src.reg = TIS_REGISTER_LEFT;
                } else if(strcasecmp(temp, "RIGHT") == 0) {
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->src.reg = TIS_REGISTER_RIGHT;
                } else if(strcasecmp(temp, "ANY") == 0) {
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->src.reg = TIS_REGISTER_ANY;
                } else if(strcasecmp(temp, "LAST") == 0) {
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->src.reg = TIS_REGISTER_LAST;
                } else {
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_NONE; // TODO give warning; unparseable argument. Give separate err for BAK?
                }
            }
            if(nargs > 1) {
                temp = strtok(NULL, " ,");
                if(temp == NULL) {
                    node->code[line]->dst.type = TIS_OP_ARG_TYPE_NONE;
                } else if(strcasecmp(temp, "ACC") == 0) {
                    node->code[line]->dst.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->dst.reg = TIS_REGISTER_ACC;
                } else if(strcasecmp(temp, "NIL") == 0) {
                    node->code[line]->dst.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->dst.reg = TIS_REGISTER_NIL;
                } else if(strcasecmp(temp, "UP") == 0) {
                    node->code[line]->dst.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->dst.reg = TIS_REGISTER_UP;
                } else if(strcasecmp(temp, "DOWN") == 0) {
                    node->code[line]->dst.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->dst.reg = TIS_REGISTER_DOWN;
                } else if(strcasecmp(temp, "LEFT") == 0) {
                    node->code[line]->dst.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->dst.reg = TIS_REGISTER_LEFT;
                } else if(strcasecmp(temp, "RIGHT") == 0) {
                    node->code[line]->dst.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->dst.reg = TIS_REGISTER_RIGHT;
                } else if(strcasecmp(temp, "ANY") == 0) {
                    node->code[line]->dst.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->dst.reg = TIS_REGISTER_ANY;
                } else if(strcasecmp(temp, "LAST") == 0) {
                    node->code[line]->dst.type = TIS_OP_ARG_TYPE_REGISTER;
                    node->code[line]->dst.reg = TIS_REGISTER_LAST;
                } else {
                    node->code[line]->dst.type = TIS_OP_ARG_TYPE_NONE; // TODO give warning; unparseable argument. Give separate err for BAK?
                }
            }

            // TODO ensure nothing else (except whitespace) is on this line
        } else {
            if(id < 0) {
                warn("Ignoring out-of-node data at top of file:\n");
            } else {
                warn("Ignoring out-of-node data after node @%d:\n", id);
            }
            warn("    %.*s\n", BUFSIZE, buf);
        }

        line++;
    }

    fclose(source);
    return INIT_OK;
}

/*
 * Frees the contents of this TIS object (but not the object itself).
 * Does not clean any non-pointer values.
 */
void destroy(tis_t tis) {
    safe_free(tis.name);
    safe_free_list(tis.nodes, tis.size, safe_free_node);
    safe_free_list(tis.inputs, tis.cols, safe_free_io_node);
    safe_free_list(tis.outputs, tis.cols, safe_free_io_node);
}

/*
 * Returns a true value if the system is quiescent.
 * This means that no node is actively running.
 * Unless waiting for additional input, the execution is done.
 */
int tick(tis_t* tis) {
    int quiescent = 1;
    char deferred[tis->size];
    for(size_t i = 0; i < tis->size; i++) {
        tis_node_state_t state = run(tis, tis->nodes[i]);
        deferred[i] = (state == TIS_NODE_STATE_WRITE_WAIT);
        if(!deferred[i]) {
            quiescent = quiescent && state != TIS_NODE_STATE_RUNNING && state == tis->nodes[i]->laststate;
            tis->nodes[i]->laststate = state;
        }
    }
    for(size_t i = 0; i < tis->size; i++) {
        if(deferred[i]) {
            tis_node_state_t state = run_defer(tis, tis->nodes[i]);
            quiescent = quiescent && state != TIS_NODE_STATE_RUNNING && state == tis->nodes[i]->laststate;
            tis->nodes[i]->laststate = state;
        }
    }
    debug("quiescent = %d\n", quiescent);
    return quiescent;
}

void print_usage(char* progname) {
    fprintf(stderr,
        "%s <source>\n"
        "%s <source> <layout>\n"
        "%s <source> <rows> <cols>\n",
        progname, progname, progname);
}

/*
 * Usage is:
 * ./tis <source>
 * ./tis <source> <layout>
 * ./tis <source> <rows> <cols>
 */
int main(int argc, char** argv) {
    tis_t tis = {0};
    char* layoutfile = NULL;

    switch(argc) {
        case 2:
            tis.rows = 3;
            tis.cols = 4;
            break;
        case 3:
            layoutfile = argv[2];
            break;
        case 4:
            tis.rows = atoi(argv[2]);
            tis.cols = atoi(argv[3]);
            break;
        default:
            print_usage(argv[0]);
            return 0;
    }

    if(init_layout(&tis, layoutfile) != INIT_OK) {
        // an error has happened, message printed from init_layout
        destroy(tis);
        return -1;
    }

    if(init_nodes(&tis, argv[1]) != INIT_OK) {
        // an error has happened, message printed from init_nodes
        destroy(tis);
        return -1;
    }

    int timelimit = 20;
    for(int time = 0; !tick(&tis) && time<timelimit; time++) {
        // nothing
    }

    destroy(tis);
    return 0;
}

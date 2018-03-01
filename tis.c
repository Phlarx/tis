#define _POSIX_C_SOURCE 200809L // for strdup()
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "tis_types.h"
#include "tis_node.h"

#define INIT_OK 0
#define INIT_FAIL 1

#define BUFSIZE 100

tis_t tis = {0};

int init_layout(tis_t* tis, char* layoutfile) {
    FILE* layout = NULL;
    if(layoutfile != NULL) {
        layout = fopen(layoutfile, "r");
    }
    if(layout != NULL) {
        // set size from file
        if(fscanf(layout, " %zu %zu ", &(tis->rows), &(tis->cols)) != 2) {
            if(feof(layout) || ferror(layout)) {
                error("Unexpected EOF when parsing dimensions\n");
            } else {
                error("Unexpected token when parsing dimensions\n");
            }
            fclose(layout);
            return INIT_FAIL;
        }
        debug("Read dimensions %zur %zuc\n", tis->rows, tis->cols);
    }

    tis->size = tis->rows*tis->cols;

    if(tis->size == 0) {
        if(layout != NULL) {
            fclose(layout);
        }
        error("Cannot initialize with zero rows or columns\n"); // But zero rows are fine, right? TODO this would mean I have to run the outputs separately from the bottom row
        return INIT_FAIL;
    }

    tis->nodes = calloc(tis->size, sizeof(tis_node_t*));
    tis->inputs = calloc(tis->cols, sizeof(tis_io_node_t*));
    tis->outputs = calloc(tis->cols, sizeof(tis_io_node_t*));

    if(layout != NULL) {
        // init node layout from file
        int id = 0;
        for(size_t i = 0; i < tis->size; i++) {
            int ch;
            while(isspace(ch = fgetc(layout))) {
                // discard whitespace
            }
            switch(ch) {
                case 'C': // compute
                case 'c':
                    tis->nodes[i] = calloc(1, sizeof(tis_node_t));
                    tis->nodes[i]->type = TIS_NODE_TYPE_COMPUTE;
                    tis->nodes[i]->id = id++;
                    tis->nodes[i]->row = i / tis->cols;
                    tis->nodes[i]->col = i % tis->cols;
                    tis->nodes[i]->writereg = TIS_REGISTER_INVALID;
                    tis->nodes[i]->last = TIS_REGISTER_INVALID;
                    break;
                case 'M': // memory (assume stack memory)
                case 'm':
                case 'S': // stack memory
                case 's':
                    error("Not yet implemented\n");
                    return INIT_FAIL;
                case 'R': // random access memory
                case 'r':
                    error("Not yet implemented\n");
                    return INIT_FAIL;
                case 'D': // damaged
                case 'd':
                    error("Not yet implemented\n");
                    return INIT_FAIL;
                case EOF:
                    error("Unexpected EOF while reading node specifiers\n");
                    fclose(layout);
                    return INIT_FAIL;
                default:
                    error("Unrecognized node specifier '%c'\n", ch);
                    fclose(layout);
                    return INIT_FAIL;
            }
        }

        // init io node layout from file
        size_t index;
        char buf[BUFSIZE];
        int mode = -1; // -1 is invalid, 0 is input, 1 is output, 2 is ignore
        while(!feof(layout) && !ferror(layout)) {
            if(fscanf(layout, " I%zu ", &index) == 1) {
                debug("Found an input for index %zu\n", index);
                if(index >= tis->cols) {
                    warn("Input I%zu is out-of-bounds for the current layout, ignoring definition\n", index);
                    mode = 2;
                    continue;
                }
                mode = 0;
                tis->inputs[index] = calloc(1, sizeof(tis_io_node_t));
                tis->inputs[index]->type = TIS_IO_TYPE_INVALID;
            } else if(fscanf(layout, " O%zu ", &index) == 1) {
                debug("Found an output for index %zu\n", index);
                if(index >= tis->cols) {
                    warn("Output O%zu is out-of-bounds for the current layout, ignoring definition\n", index);
                    mode = 2;
                    continue;
                }
                mode = 1;
                tis->outputs[index] = calloc(1, sizeof(tis_io_node_t));
            } else if(fscanf(layout, " %100s ", buf) == 1) { // NOTE: I don't see an easy way to make this 100 rely on BUFSIZE
                switch(mode) {
                    case 0:
                        if(tis->inputs[index]->type == TIS_IO_TYPE_INVALID) {
                            if(strcasecmp(buf, "ASCII") == 0) {
                                debug("Set I%zu to ASCII mode\n", index);
                                tis->inputs[index]->type = TIS_IO_TYPE_IOSTREAM_ASCII;
                            } else {
                                goto skip_io_token;
                            }
                        } else if(tis->inputs[index]->type == TIS_IO_TYPE_IOSTREAM_ASCII) {
                            if(strcasecmp(buf, "STDIN") == 0 ||
                               strcasecmp(buf, "-") == 0) {
                                debug("Set I%zu to use stdin\n", index);
                                tis->inputs[index]->file = stdin; // TODO make sure this doesn't already have a file
                            } else {
                                debug("Set I%zu to use file %.*s\n", index, BUFSIZE, buf);
                                if((tis->inputs[index]->file = fopen(buf, "r")) == NULL) { // TODO register file for later close?
                                    error("Unable to open %.*s for reading\n", BUFSIZE, buf); // TODO what to do about this? error out?
                                }
                            }
                        } else {
                            // TODO node type not implemented? internal error?
                        }
                        break;
                    case 1:
                        if(tis->outputs[index]->type == TIS_IO_TYPE_INVALID) {
                            if(strcasecmp(buf, "ASCII") == 0) {
                                debug("Set O%zu to ASCII mode\n", index);
                                tis->outputs[index]->type = TIS_IO_TYPE_IOSTREAM_ASCII;
                            } else {
                                goto skip_io_token;
                            }
                        } else if(tis->outputs[index]->type == TIS_IO_TYPE_IOSTREAM_ASCII) {
                            if(strcasecmp(buf, "STDOUT") == 0 ||
                               strcasecmp(buf, "-") == 0) {
                                debug("Set O%zu to use stdout\n", index);
                                tis->outputs[index]->file = stdout; // TODO make sure this doesn't already have a file
                            } else if(strcasecmp(buf, "STDERR") == 0) {
                                debug("Set O%zu to use stderr\n", index);
                                tis->outputs[index]->file = stderr;
                            } else {
                                debug("Set O%zu to use file %.*s\n", index, BUFSIZE, buf);
                                if((tis->outputs[index]->file = fopen(buf, "a")) == NULL) { // TODO register file for later close?
                                    error("Unable to open %.*s for writing\n", BUFSIZE, buf); // TODO what to do about this? error out?
                                }
                            }
                        } else {
                            // TODO node type not implemented? internal error?
                        }
                        break;
                    case 2:
                        debug("Skipping past token %.*s\n", BUFSIZE, buf);
                        break;
                    case -1:
                    default:
skip_io_token:
                        error("Found unexpected token %.*s, ignoring\n", BUFSIZE, buf);
                        break;
                }
            }
        }

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
        tis->inputs[0]->file = stdin;
        // set last output to TIS_IO_TYPE_IOSTREAM_NUMERIC
        tis->outputs[tis->cols - 1] = calloc(1, sizeof(tis_io_node_t));
        tis->outputs[tis->cols - 1]->type = TIS_IO_TYPE_IOSTREAM_ASCII;
        tis->outputs[tis->cols - 1]->file = stdout;
    }

    return INIT_OK;
}

int init_nodes(tis_t* tis, char* sourcefile) {
    FILE* source = fopen(sourcefile, "r");

    char buf[BUFSIZE];
    int id = -1, preid = -1;
    int line = TIS_NODE_LINE_COUNT; // start with an out-of-bounds value
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
            // empty line; ignore
            // (enforce empty line before new node?)
            // TODO experiment: what does the game do in this case?
        } else if(sscanf(buf, "@%d", &id) == 1) { // TODO check for extra data on this line (experiment: what does the game do with this?)
            if(id < preid) {
                // TODO experiment: what does the game do in this case?
                warn("Nodes appear out of order, @%d is after @%d. Continuing anyway.\n", id, preid);
                preid = id;
            }
            node = NULL;
            line = -1; // will be zero next line
            for(size_t i = 0; i < tis->size; i++) {
                if(tis->nodes[i]->type == TIS_NODE_TYPE_COMPUTE && tis->nodes[i]->id == id) {
                    node = tis->nodes[i];
                    break;
                }
            }
            if(node == NULL) {
                // TODO experiment: what does the game do in this case?
                warn("@%d is out-of-bounds for the current layout. Contents will be ignored.\n", id);
            } else if(node->code[0] != NULL) {
                // TODO experiment: what does the game do in this case?
                warn("@%d has already been seen. Contents will be ignored.\n", id);
                node = NULL;
            }
        } else if(node == NULL && line < TIS_NODE_LINE_COUNT) {
            // Nothing to do, just skipping past these lines
        } else if(node != NULL && line < TIS_NODE_LINE_COUNT) {
            if(strlen(buf) > TIS_NODE_LINE_LENGTH) {
                // TODO experiment: what does the game do in this case?
                warn("Overlength line, continuing anyway:\n");
                warn("    %.*s\n", BUFSIZE, buf);
            }
            node->code[line] = calloc(1, sizeof(tis_op_t));
            node->code[line]->linenum = line+1; // these are 1-indexed
            node->code[line]->linetext = strdup(buf);

            // TODO parse breakpoints (!) (possible future enhancement)

            char* temp = NULL;
            char* temp2 = NULL;
            int val = 0;
            int nargs = 0;
            if(tis->name == NULL) {
                if((temp = strstr(buf, "##")) != NULL) { // Save title, if present
                    temp += 2; // skip past ##
                    temp = strtok(temp, " "); // strip whitespace
                    tis->name = strdup(temp);
                }
            }
            if((temp = strchr(buf, '#')) != NULL) { // Remove comment, if present
                *temp = '\0';
            }
            if((temp = strchr(buf, ':')) != NULL) { // Save and remove label, if present
                *temp = '\0';
                temp++; // temp now points just after label
                node->code[line]->label = strdup(buf); // TODO strip whitespace? (but this would be invalid for real TIS), verify label is A-Z0-9~`$%^&*()_-+={}[]|\;"'<>,.?/,
            } else {                                   // labels may be 18 chars (whole line + ':') but longest useful is 14 (for jmp <label>)??? off by one???
                temp = buf;
            }

            temp = strtok(temp, " ,");
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
                error("Unrecognized instruction \"%s\" on line %d of @%d\n", temp, line+1, id);
                node->code[line]->type = TIS_OP_TYPE_INVALID;
                nargs = 0;
            }
            if(nargs > 0) {
                temp = strtok(NULL, " ,");
                if(temp == NULL) {
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_NONE;
                } else if(val == 1) { // labels are only valid as sources (...syntactically. semantically, they are a dst; syntactically, they are actually a src)
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_LABEL; // TODO experiment: are register keywords valid as labels? are numerics valid as labels?
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
                    error("Invalid first argument \"%s\" on line %d of @%d", temp, line+1, id);
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_NONE; // TODO Give separate err for BAK?
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
                    error("Invalid second argument \"%s\" on line %d of @%d", temp, line+1, id);
                    node->code[line]->dst.type = TIS_OP_ARG_TYPE_NONE; // TODO Give separate err for BAK?
                }
            }

            // ensure nothing else (except whitespace) is on this line
            while((temp = strtok(NULL, " ,")) != NULL) {
                // TODO experiment: what does the game do in this case?
                error("Extra token \"%s\" on line %d of @%d\n", temp, line+1, id);
            }
        } else {
            if(id < 0) {
                warn("Ignoring out-of-node data at top of file:\n");
            } else {
                warn("Ignoring out-of-node data after @%d:\n", id);
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
 * This endeavors to close any open file handles, free all memory, etc, before exiting.
 * (register via atexit).
 */
void pre_exit() {
    destroy(tis);
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

/*
 * Usage is:
 * ./tis <source>
 * ./tis <source> <layout>
 * ./tis <source> <rows> <cols>
 */
void print_usage(char* progname) {
    fprintf(stderr,
        "%s <source>\n"
        "%s <source> <layout>\n"
        "%s <source> <rows> <cols>\n",
        progname, progname, progname);
    // TODO flesh this out a bit
}

int main(int argc, char** argv) {
    atexit(pre_exit);

    char* layoutfile = NULL;
    int timelimit = 0;

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
        // an error has happened, message was printed from init_layout
        exit(EXIT_FAILURE);
        //destroy(tis);
        //return -1;
    }

    if(init_nodes(&tis, argv[1]) != INIT_OK) {
        // an error has happened, message was printed from init_nodes
        exit(EXIT_FAILURE);
        //destroy(tis);
        //return -1;
    }

    for(int time = 0; !tick(&tis) && (timelimit == 0 || time < timelimit); time++) {
        // nothing
    }

    exit(EXIT_SUCCESS);
    //destroy(tis);
    //return 0;
}

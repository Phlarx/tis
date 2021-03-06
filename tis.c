#define _POSIX_C_SOURCE 200809L // for strdup() and fmemopen()
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "tis_types.h"
#include "tis_node.h"
#include "tis_io.h"

#define INIT_OK 0
#define INIT_FAIL 1

#define BUFSIZE 128

#define STR(x) _STR(x)
#define _STR(x) #x

tis_t tis = {0};
tis_opt_t opts = {0};

/*
 * This is a linked list of file handles to close when destroying things.
 * This is to be used only for file handles that are non-trivial to close the normal way.
 * This can cause double-frees if not used with care.
 */
typedef struct file_list {
    FILE* file;
    struct file_list* next;
} file_list_t;
static file_list_t* files_to_close = NULL;
void register_file_handle(FILE* file) {
    file_list_t* temp = calloc(1, sizeof(file_list_t));
    temp->file = file;
    temp->next = files_to_close;
    files_to_close = temp;
}
void close_file_handles() {
    while(files_to_close != NULL) {
        fclose(files_to_close->file);
        file_list_t* temp = files_to_close->next;
        free(files_to_close);
        files_to_close = temp;
    }
}

/*
 * Parse the layout file, allocate structural memory, initialize all things
 */
int init_layout(tis_t* tis, char* layoutfile, int layoutmode) {
    FILE* layout = NULL;
    if(layoutfile != NULL) {
        if(layoutmode == 0) { // default mode: layoutfile is a filename
            if(strcasecmp(layoutfile, "-") == 0) {
                layout = stdin;
            } else {
                layout = fopen(layoutfile, "r");
                if(layout == NULL) {
                    error("Unable to open layout file '%s' for reading\n", layoutfile);
                    return INIT_FAIL;
                }
            }
        } else { // alternate mode: layoutfile is a string representing the file contents
            layout = fmemopen(layoutfile, strlen(layoutfile), "r");
            if(layout == NULL) {
                error("Unable to prepare layout string for reading\n");
                return INIT_FAIL;
            }
        }
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
        debug("Read dimensions %zur %zuc from layout '%s'\n", tis->rows, tis->cols, layoutfile);
    }

    tis->size = tis->rows*tis->cols;

    if(tis->cols == 0) {
        if(layout != NULL) {
            fclose(layout);
        }
        error("Cannot initialize with zero columns\n"); // But zero rows are fine, it works as a translator: printf "hello" | ./tis -l /dev/null "0 1 I0 ASCII - O0 NUMERIC - 10"
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
            tis->nodes[i] = calloc(1, sizeof(tis_node_t));
            tis->nodes[i]->row = i / tis->cols;
            tis->nodes[i]->col = i % tis->cols;
            tis->nodes[i]->writereg = TIS_REGISTER_INVALID;
            tis->nodes[i]->id = -1; // This is overwritten for compute nodes only
            switch(ch) {
                case 'C': // compute
                case 'c':
                    tis->nodes[i]->type = TIS_NODE_TYPE_COMPUTE;
                    tis->nodes[i]->id = id++;
                    tis->nodes[i]->last = TIS_REGISTER_NIL; // LAST behaves like NIL until an ANY occurs
                    tis->nodes[i]->name = strdup("COMPUTE");
                    break;
                case 'M': // memory (assume stack memory)
                case 'm':
                case 'S': // stack memory
                case 's':
                    tis->nodes[i]->type = TIS_NODE_TYPE_MEMORY_STACK;
                    tis->nodes[i]->index = 0;
                    tis->nodes[i]->name = strdup("STACK");
                    break;
                case 'R': // random access memory
                case 'r':
                    tis->nodes[i]->type = TIS_NODE_TYPE_MEMORY_RAM;
                    tis->nodes[i]->index = 0;
                    tis->nodes[i]->name = strdup("RAM");
                    error("Node type not yet implemented\n");
                    fclose(layout);
                    return INIT_FAIL;
                case 'D': // damaged / disabled
                case 'd':
                    tis->nodes[i]->type = TIS_NODE_TYPE_DAMAGED;
                    tis->nodes[i]->name = strdup("DAMAGED");
                    break;
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
                tis->inputs[index]->col = index;
                tis->inputs[index]->type = TIS_IO_TYPE_INVALID;
                tis->inputs[index]->writereg = TIS_REGISTER_INVALID;
            } else if(fscanf(layout, " O%zu ", &index) == 1) {
                debug("Found an output for index %zu\n", index);
                if(index >= tis->cols) {
                    warn("Output O%zu is out-of-bounds for the current layout, ignoring definition\n", index);
                    mode = 2;
                    continue;
                }
                mode = 1;
                tis->outputs[index] = calloc(1, sizeof(tis_io_node_t));
                tis->outputs[index]->col = index;
                tis->outputs[index]->type = TIS_IO_TYPE_INVALID;
                tis->outputs[index]->writereg = TIS_REGISTER_INVALID;
            } else if(fscanf(layout, " %"STR(BUFSIZE)"s ", buf) == 1) { // The format string is " %128s ", but changes with BUFSIZE
                switch(mode) {
                    case 0:
                        if(tis->inputs[index]->type == TIS_IO_TYPE_INVALID) {
                            if(strcasecmp(buf, "ASCII") == 0) {
                                debug("Set I%zu to ASCII mode\n", index);
                                tis->inputs[index]->type = TIS_IO_TYPE_IOSTREAM_ASCII;
                            } else if(strcasecmp(buf, "NUMERIC") == 0) {
                                debug("Set I%zu to NUMERIC mode\n", index);
                                tis->inputs[index]->type = TIS_IO_TYPE_IOSTREAM_NUMERIC;
                            } else {
                                goto skip_io_token;
                            }
                        } else if(tis->inputs[index]->type == TIS_IO_TYPE_IOSTREAM_ASCII ||
                                  tis->inputs[index]->type == TIS_IO_TYPE_IOSTREAM_NUMERIC) {
                            if(tis->inputs[index]->file.file == NULL) {
                                if(strcasecmp(buf, "STDIN") == 0 ||
                                    strcasecmp(buf, "-") == 0) {
                                    debug("Set I%zu to use stdin\n", index);
                                    tis->inputs[index]->file.file = stdin;
                                } else {
                                    debug("Set I%zu to use file %.*s\n", index, BUFSIZE, buf);
                                    if((tis->inputs[index]->file.file = fopen(buf, "r")) == NULL) {
                                        error("Unable to open %.*s for reading, will provide no data instead\n", BUFSIZE, buf);
                                    }
                                    register_file_handle(tis->inputs[index]->file.file);
                                }
                            } else {
                                goto skip_io_token;
                            }
                        } else {
                            // TODO io node type not implemented? internal error?
                            goto skip_io_token;
                        }
                        break;
                    case 1:
                        if(tis->outputs[index]->type == TIS_IO_TYPE_INVALID) {
                            if(strcasecmp(buf, "ASCII") == 0) {
                                debug("Set O%zu to ASCII mode\n", index);
                                tis->outputs[index]->type = TIS_IO_TYPE_IOSTREAM_ASCII;
                            } else if(strcasecmp(buf, "NUMERIC") == 0) {
                                debug("Set O%zu to NUMERIC mode\n", index);
                                tis->outputs[index]->type = TIS_IO_TYPE_IOSTREAM_NUMERIC;
                                tis->outputs[index]->file.sep = -1;
                            } else {
                                goto skip_io_token;
                            }
                        } else if(tis->outputs[index]->type == TIS_IO_TYPE_IOSTREAM_ASCII ||
                                  tis->outputs[index]->type == TIS_IO_TYPE_IOSTREAM_NUMERIC) {
                            if(tis->outputs[index]->file.file == NULL) {
                                if(strcasecmp(buf, "STDOUT") == 0 ||
                                    strcasecmp(buf, "-") == 0) {
                                    debug("Set O%zu to use stdout\n", index);
                                    tis->outputs[index]->file.file = stdout;
                                } else if(strcasecmp(buf, "STDERR") == 0) {
                                    debug("Set O%zu to use stderr\n", index);
                                    tis->outputs[index]->file.file = stderr;
                                } else {
                                    debug("Set O%zu to use file %.*s\n", index, BUFSIZE, buf);
                                    if((tis->outputs[index]->file.file = fopen(buf, "a")) == NULL) {
                                        error("Unable to open %.*s for writing, will silently drop data instead\n", BUFSIZE, buf);
                                    }
                                    register_file_handle(tis->outputs[index]->file.file);
                                }
                            } else if(tis->outputs[index]->type == TIS_IO_TYPE_IOSTREAM_NUMERIC &&
                                      sscanf(buf, "%d", &(tis->outputs[index]->file.sep)) == 1) {
                                debug("Set O%zu separator to %d\n", index, tis->outputs[index]->file.sep);
                            } else {
                                goto skip_io_token;
                            }
                        } else {
                            // TODO io node type not implemented? internal error?
                            goto skip_io_token;
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

        if(layout != stdin) {
            fclose(layout);
        }
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
            tis->nodes[i]->last = TIS_REGISTER_NIL; // LAST behaves like NIL until an ANY occurs
            tis->nodes[i]->name = strdup("COMPUTE");
        }
        // set first input to TIS_IO_TYPE_IOSTREAM_NUMERIC
        tis->inputs[0] = calloc(1, sizeof(tis_io_node_t));
        tis->inputs[0]->col = 0;
        tis->inputs[0]->type = opts.default_i_type;
        tis->inputs[0]->file.file = stdin;
        tis->inputs[0]->writereg = TIS_REGISTER_INVALID;
        // set last output to TIS_IO_TYPE_IOSTREAM_NUMERIC
        tis->outputs[tis->cols - 1] = calloc(1, sizeof(tis_io_node_t));
        tis->outputs[tis->cols - 1]->col = tis->cols - 1;
        tis->outputs[tis->cols - 1]->type = opts.default_o_type;
        tis->outputs[tis->cols - 1]->file.file = stdout;
        tis->outputs[tis->cols - 1]->file.sep = '\n';
        tis->outputs[tis->cols - 1]->writereg = TIS_REGISTER_INVALID;
    }

    return INIT_OK;
}

/*
 * Parse and load the code from the source file into the compute nodes.
 * Other nodes types need not be touched here.
 */
int init_nodes(tis_t* tis, char* sourcefile) {
    FILE* source = NULL;
    if(strcasecmp(sourcefile, "-") == 0) {
        source = stdin;
    } else {
        source = fopen(sourcefile, "r");
        if(source == NULL) {
            error("Unable to open source file '%s' for reading\n", sourcefile);
            return INIT_FAIL;
        }
    }

    char buf[BUFSIZE], extra;
    int id = -1, preid = -1, nfields;
    int line = TIS_NODE_LINE_COUNT; // start with an out-of-bounds value
    tis_node_t* node = NULL;
    while(fgets(buf, BUFSIZE, source) != NULL) {
        char* nl = strchr(buf, '\n');
        if(nl == NULL) {
            if(!feof(source)) {
                error("Line too long, unexpected things may occur:\n");
                error("    %.*s\n", BUFSIZE, buf);
            } else {
                // this is fine and normal
            }
        } else {
            *nl = '\0';
        }

        spam("Parse line:  %.*s\n", BUFSIZE, buf);

        if(buf[0] == '\0' && line >= TIS_NODE_LINE_COUNT) {
            // empty line; ignore
            // (when game writes saves, it adds an extra blank line at the end of each node, but doesn't require them for parsing)
        } else if((nfields = sscanf(buf, "@%d %c", &id, &extra)) >= 1) {
            if(nfields > 1) {
                // TODO strict mode: the game just ignores this whole line
                error("Extra data appears on specifier line for @%d. Continuing anyway.\n", id);
            }
            if(id < preid) {
                // the game handles reorderings silently
                warn("Nodes appear out of order, @%d is after @%d. Continuing anyway.\n", id, preid);
            }
            preid = id;
            node = NULL;
            line = -1; // will be zero next line
            for(size_t i = 0; i < tis->size; i++) {
                if(tis->nodes[i]->type == TIS_NODE_TYPE_COMPUTE && tis->nodes[i]->id == id) {
                    node = tis->nodes[i];
                    break;
                }
            }
            if(node == NULL) {
                // the game just adds the code to the last node, we ignore it instead
                warn("@%d is out-of-bounds for the current layout. Contents will be ignored.\n", id);
            } else if(node->code[0] != NULL) {
                // replace the previous node contents with the new
                warn("@%d has already been seen. Previous contents will be discarded and replaced.\n", id);
                for(int idx = 0; idx < TIS_NODE_LINE_COUNT; idx++) {
                    safe_free_op(node->code[idx]);
                }
            }
        } else if(node == NULL && line < TIS_NODE_LINE_COUNT) {
            // Nothing to do, just skipping past these lines
        } else if(node != NULL && line < TIS_NODE_LINE_COUNT) {
            if(strlen(buf) > TIS_NODE_LINE_LENGTH) {
                // TODO strict mode: truncate the line unconditionally
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
                // the game ignores any title beyond the first
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
                node->code[line]->label = strdup(buf); // TODO strip whitespace? (but rstrip would be invalid for real TIS), verify label is A-Z0-9~`$%^&*()_-+={}[]|\;"'<>,.?/,
            } else {                                   // labels may be 17 chars (whole line + ':') but longest useful is 14 (for jmp <label>)
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
                error("Unrecognized opcode \"%s\" on line %d of @%d\n", temp, line+1, id);
                node->code[line]->type = TIS_OP_TYPE_INVALID;
                nargs = 0;
            }
            if(nargs > 0) {
                temp = strtok(NULL, " ,");
                if(temp == NULL) {
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_NONE;
                } else if(val == 1) { // labels are only valid as sources (...syntactically. semantically, they are a dst; syntactically, they are actually a src)
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_LABEL; // note: the label type overrides everything else; "MOV" and "16" are both valid as labels
                    node->code[line]->src.label = strdup(temp); // whitespace is already stripped by strtok
                } else if((val = strtol(temp, &temp2, 0), temp != temp2 && *temp2 == '\0')) { // constants are only valid as sources
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_CONSTANT;
                    node->code[line]->src.con = clamp(val);
                    if(node->code[line]->src.con != val) {
                        // produce a warning if the value is clamped
                        warn("Numeric operand %d is clamped to %d on line %d of @%d\n", val, clamp(val), line+1, id);
                    }
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
                    error("Invalid first operand \"%s\" on line %d of @%d\n", temp, line+1, id);
                    node->code[line]->src.type = TIS_OP_ARG_TYPE_NONE; // This error also catches BAK usage
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
                    error("Invalid second operand \"%s\" on line %d of @%d\n", temp, line+1, id);
                    node->code[line]->dst.type = TIS_OP_ARG_TYPE_NONE; // This error also catches BAK usage
                }
            }

            // ensure nothing else (except whitespace) is on this line
            while((temp = strtok(NULL, " ,")) != NULL) {
                // TODO strict mode: return INIT_FAIL
                error("Extra operand \"%s\" on line %d of @%d\n", temp, line+1, id);
            }
        } else {
            // the game just ignores most extra lines, we ignore all
            if(id < 0) {
                warn("Ignoring out-of-node data at top of file:\n");
            } else {
                warn("Ignoring out-of-node data after @%d:\n", id);
            }
            warn("    %.*s\n", BUFSIZE, buf);
        }

        line++;
    }

    if(source != stdin) {
        fclose(source);
    }
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
    int offset = tis->cols;
    char deferred_all[tis->size + 2*offset];
    char *deferred_i = deferred_all;
    char *deferred_n = deferred_all + offset;
    char *deferred_o = deferred_all + offset + tis->size;

    // First stage: run most things
    for(size_t i = 0; i < tis->cols; i++) {
        if(tis->inputs[i] != NULL) {
            tis_node_state_t state = run_input(tis, tis->inputs[i]);
            deferred_i[i] = (state == TIS_NODE_STATE_WRITE_WAIT);
            if(!deferred_i[i]) {
                quiescent = quiescent && state != TIS_NODE_STATE_RUNNING && state == tis->inputs[i]->laststate;
                tis->inputs[i]->laststate = state;
            }
        }
    }
    for(size_t i = 0; i < tis->size; i++) {
        tis_node_state_t state = run(tis, tis->nodes[i]);
        deferred_n[i] = (state == TIS_NODE_STATE_WRITE_WAIT);
        if(!deferred_n[i]) {
            quiescent = quiescent && state != TIS_NODE_STATE_RUNNING && state == tis->nodes[i]->laststate;
            tis->nodes[i]->laststate = state;
        }
    }
    for(size_t i = 0; i < tis->cols; i++) {
        if(tis->outputs[i] != NULL) {
            tis_node_state_t state = run_output(tis, tis->outputs[i]);
            deferred_o[i] = (state == TIS_NODE_STATE_WRITE_WAIT);
            if(!deferred_o[i]) {
                quiescent = quiescent && state != TIS_NODE_STATE_RUNNING && state == tis->outputs[i]->laststate;
                tis->outputs[i]->laststate = state;
            }
        }
    }

    // Second stage: run deferrals
    for(size_t i = 0; i < tis->cols; i++) {
        if(tis->inputs[i] != NULL) {
            if(deferred_i[i]) {
                tis_node_state_t state = run_input_defer(tis, tis->inputs[i]);
                quiescent = quiescent && state != TIS_NODE_STATE_RUNNING && state == tis->inputs[i]->laststate;
                tis->inputs[i]->laststate = state;
            }
        }
    }
    for(size_t i = 0; i < tis->size; i++) {
        if(deferred_n[i]) {
            tis_node_state_t state = run_defer(tis, tis->nodes[i]);
            quiescent = quiescent && state != TIS_NODE_STATE_RUNNING && state == tis->nodes[i]->laststate;
            tis->nodes[i]->laststate = state;
        }
    }
    for(size_t i = 0; i < tis->cols; i++) {
        if(tis->outputs[i] != NULL) {
            if(deferred_o[i]) {
                tis_node_state_t state = run_output_defer(tis, tis->outputs[i]);
                quiescent = quiescent && state != TIS_NODE_STATE_RUNNING && state == tis->outputs[i]->laststate;
                tis->outputs[i]->laststate = state;
            }
        }
    }

    spam("System quiescent? %d\n", quiescent);
    return quiescent;
}

/*
 * Usage is:
 * ./tis <source>
 * ./tis <source> <layout>
 * ./tis <source> <rows> <cols>
 */
void print_usage(char* progname) {
    fprintf(stderr, "Usage:\n"
        "    %s [opts] <source>\n"
        "    %s [opts] <source> <layout>\n"
        "    %s [opts] <source> <rows> <cols>\n\n",
        progname, progname, progname);
    fprintf(stderr, "Options:\n"
        "    -c      cycle limit; prevent the emulator from running\n"
        "                for more than this many cycles\n"
        "    -h      help; show this text\n"
        "    -l      layout string; layout is given as a string\n"
        "                instead of a file name\n"
        "    -n      numeric; change the default layout to use\n"
        "                numeric io instead of ascii, only\n"
        "                relevant when not using a custom layout\n"
        "    -q      quiet; decrease verbosity by one level,\n"
        "                may be provided multiple times\n"
        "    -v      verbose; increase verbosity by one level,\n"
        "                may be provided multiple times\n\n");
    // TODO flesh this out a bit more
}

int main(int argc, char** argv) {
    atexit(pre_exit);
    atexit(close_file_handles);

    int MAXARGS = 3;
    char* argvector[MAXARGS];
    int argcount = 0;
    char* sourcefile = NULL;
    char* layoutfile = NULL;
    int timelimit = 0;
    int layoutmode = 0;

    opts.verbose = 0;
    opts.default_i_type = TIS_IO_TYPE_IOSTREAM_ASCII;
    opts.default_o_type = TIS_IO_TYPE_IOSTREAM_ASCII;

    int c;
    while((c = getopt(argc, argv, "-c:hlnqv")) != -1) {
        // parse short opts
        switch(c) {
            case 'c': // cycle count limit
                timelimit = atoi(optarg); // TODO ensure that there is nothing else in this arg
                break;
            case 'h': // help
            case '?': // (this is also used for an unrecognized opt)
                print_usage(argv[0]);
                exit(EXIT_SUCCESS);
            case 'l': // layoutmode toggle
                layoutmode = 1;
                break;
            case 'n': // numeric default io
                opts.default_i_type = TIS_IO_TYPE_IOSTREAM_NUMERIC;
                opts.default_o_type = TIS_IO_TYPE_IOSTREAM_NUMERIC;
                break;
            case 'q': // quiet
                opts.verbose--;
                break;
            case 'v': // verbose
                opts.verbose++;
                break;
            case 1: // positional arg
                if(argcount >= MAXARGS) {
                    error("Too many arguments!\n");
                    print_usage(argv[0]);
                    exit(EXIT_FAILURE);
                } else {
                    argvector[argcount] = optarg;
                    argcount++;
                }
                break;
            default:
                error("Skipping unimplemented short opt '-%c'\n", c);
                break;
        }
    }

    for(; optind < argc; optind++) {
        if(argcount >= MAXARGS) {
            error("Too many arguments!\n");
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        } else {
            argvector[argcount] = argv[optind];
            argcount++;
        }
    }

    switch(argcount) { // do different things based on how many args are provided
        case 1:
            sourcefile = argvector[0];
            tis.rows = 3;
            tis.cols = 4;
            debug("Using default dimensions %zur %zuc\n", tis.rows, tis.cols);
            break;
        case 2:
            sourcefile = argvector[0];
            layoutfile = argvector[1];
            break;
        case 3:
            sourcefile = argvector[0];
            tis.rows = atoi(argvector[1]); // TODO ensure that there is nothing else in this arg
            tis.cols = atoi(argvector[2]); // TODO ensure that there is nothing else in this arg
            debug("Read dimensions %zur %zuc from command line\n", tis.rows, tis.cols);
            break;
        case 0:
            error("Too few arguments!\n");
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        default:
            error("Too many arguments!\n");
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
    }

    if(init_layout(&tis, layoutfile, layoutmode) != INIT_OK) {
        // an error has happened, message was printed from init_layout
        exit(EXIT_FAILURE);
    }

    if(init_nodes(&tis, sourcefile) != INIT_OK) {
        // an error has happened, message was printed from init_nodes
        exit(EXIT_FAILURE);
    }

    for(int time = 0; !tick(&tis) && (timelimit == 0 || time < timelimit); time++) {
        // nothing
    }

    exit(EXIT_SUCCESS);
}

CC=gcc
CFLAGS= -Wall -Wextra -Wpedantic -O3 -std=c11
#CFLAGS= -Wall -Wextra -Wpedantic -O0 -std=c11 -g
RM=rm -f

OBJECTS=tis.o tis_io.o tis_node.o tis_ops.o

tis: ${OBJECTS}

tis.o: tis_types.h tis_node.h
tis_io.o: tis_types.h
tis_node.o: tis_types.h tis_node.h tis_ops.h tis_io.h
tis_ops.o: tis_types.h tis_node.h

all: tis

clean: cleanobj cleanexe
cleanobj:
	-${RM} ${OBJECTS}
cleanexe:
	-${RM} tis

.PHONY: all clean cleanobj cleanexe

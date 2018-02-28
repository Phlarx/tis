CC=gcc
#CFLAGS= -Wall -Wextra -Wpedantic -O3 -std=c11
CFLAGS= -Wall -Wextra -Wpedantic -O0 -std=c11 -g
RM=rm -f

OBJECTS=tis.o tis_io.o tis_node.o tis_ops.o

tis: ${OBJECTS}

tis.o: tis_types.h tis_node.h
tis_io.o: tis_types.h
tis_node.o: tis_types.h tis_node.h tis_ops.h tis_io.h
tis_ops.o: tis_types.h tis_node.h

tis.f: tis.f.o tis_io.o tis_node.o tis_ops.o
	${CC} $^ -o $@
tis.f.o: tis_types.h tis_node.h
tis.f.c: tis.c tis_types.h tis_ops.h
	grep "#include <" $< > $@
	grep -v "#include <" $< > tis.temp.c
	${CPP} tis.temp.c -o tis.temp.i
	grep -v "^#" tis.temp.i >> $@
	-${RM} tis.temp.c tis.temp.i

all: tis tis.f

clean: cleanobj cleanexe cleanf
cleanobj:
	-${RM} ${OBJECTS}
cleanexe:
	-${RM} tis
cleanf:
	-${RM} tis.f tis.f.o tis.f.c

.PHONY: all clean cleanobj cleanexe cleanf

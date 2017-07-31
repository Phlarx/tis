#!/usr/bin/python3 -bb

import sys

COLS = 4
ROWS = 3

def init():
	# todo:
	# layout defaults to 3 rows 4 cols, all T21 compute nodes
	# can be defined via -n 'ccmcccccdcmc'? (c)ompute, (m)emory, (d)amaged
	# input defaults to stdin on second on top row; unless only 1, use that; unless none, no input
	# can be defined via -i 'xx-x'? (x)none, (-)stdin, (r)andom number, random (l)ist, random (i)ndex, random (p)ositive, random (n)egative...
	# output defaults to stdout on second-to-last on bottom row; unless only 1, use that; unless none, no output
	# can be defined via -o 'xx-x'? (x)none, (-)stdout, (d)rawing...
	# able to use the lua definitions instead? Maybe via Lunatic (https://labix.org/lunatic-python)? (Far in future)

	with open(sys.argv[1], 'r') as f:
		return f.readlines()

def parseLine(line):
	# now check if line is too long
	code, _, comment = line.partition('#')
	# now check if line w/o comment is too long
	label, _, cmd = tuple(code.rpartition(':'))
	# now validate label
	cmd = cmd.split()
	op =  cmd[0]
	# now validate op
	args = cmd[1:]
	# now validate args based on op
	return (label, (op, args), comment)

def parseProg(prog):
	index = None
	code = [[] for i in range(ROWS*COLS)]
	for line in prog:
		if line[0] == '@':
			if index is not None:
				# now check if previous T21 node had too many lines
				pass
			# now validate line is only 's/@[0-9]+/'
			# if not:
			# now check if line is too long
			line = line.split('#',1)[0]
			# endif
			index = int(line[1:])
			# now validate index is in range
		elif index is not None:
			code[index].append(parseLine(line))
		else:
			pass # line is ignored, it is not assigned to a T21 node
	return code

if __name__ == "__main__":
	print(parseProg(init()))

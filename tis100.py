#!/usr/bin/python3 -bb

import sys

COLS = 4
ROWS = 3

def init():
	with open(sys.argv[1], 'r') as f:
		return f.readlines()

def parse(arr):
	index = None
	code = [[] for i in range(ROWS*COLS)]
	for line in arr:
		if line[0] == '@':
			index = int(line[1:])
		elif index is not None:
			code[index].append(line.split('#',1)[0].split())
		else:
			pass # line is ignored
	return code

if __name__ == "__main__":
	print(parse(init()))

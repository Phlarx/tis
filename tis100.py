#!/usr/bin/python3 -bb

import sys

COLS = 4
ROWS = 3
INPUT = 'x-xx'
NODES = 'cccc'*ROWS
OUTPUT= 'xx-x'

###
#  TIS-100 objects
###

class Val(int):
	"""Like int, but clamped to [-999,999]
	   Supports adding, subtracting, and negation"""
	_MIN = -999
	_MAX = 999

	def __new__(cls, *args, **kwargs):
		return int.__new__(cls, *args, **kwargs)

	def __add__(self, other):
		return Val(sorted((int(self) + int(other), self._MAX))[0])
	def __sub__(self, other):
		return Val(sorted((int(self) - int(other), self._MAX))[0])
	def __neg__(self, other):
		return Val(-int(self))

class Ops(object):
	"""All the operators"""

	class add(object):
		nargs = 1

		@classmethod
		def run(cls, node, args):
			node['acc'] += node[args[0]]

	class jgz(object):
		nargs = 1

		@classmethod
		def run(cls, node, args):
			if node['acc'] > 0:
				node.jumpLabel(args[0])

	class mov(object):
		nargs = 2

		@classmethod
		def run(cls, node, args):
			node[args[1]] = node[args[2]]

	class sav(object):
		nargs = 0

		@classmethod
		def run(cls, node, args):
			node['bak'] = node['acc']

	class sub(object):
		nargs = 1

		@classmethod
		def run(cls, node, args):
			node['acc'] -= node[args[0]]

	class swp(object):
		nargs = 0

		@classmethod
		def run(cls, node, args):
			node['acc'], node['bak'] = node['bak'], node['acc']

###
#  Parser & init
###

def init():
	# todo:
	# layout defaults to 3 rows 4 cols, all T21 compute nodes
	# can be defined via -n 'ccmcccccdmcc'? (c)ompute, (m)emory, (d)amaged
	# input defaults to stdin on second on top row; unless only 1, use that; unless none, no input
	# can be defined via -i 'x-xx'? (x)none, (-)stdin (c)har, (s)tdin number, (r)andom number, random (l)ist, random (i)ndex, random (p)ositive, random (n)egative...
	# output defaults to stdout on second-to-last on bottom row; unless only 1, use that; unless none, no output
	# can be defined via -o 'xx-x'? (x)none, (-)stdout (c)har, (s)tdout number, (d)rawing...
	# able to use the lua definitions instead? Maybe via Lunatic (https://labix.org/lunatic-python)? (Far in future)
	#
	# what happens when multiple stdins/stdouts?

	with open(sys.argv[1], 'r') as f:
		return f.readlines()

def parseOp(op):
	return Ops.__getattribute__(Ops, op.lower())

def parseArg(arg):
	return arg.lower()

def parseLine(line):
	print(line)
	# now check if line is too long
	code, _, comment = line.partition('#')
	comment = comment.strip()
	# now check if line w/o comment is too long
	label, _, cmd = tuple(code.rpartition(':'))
	# now validate label
	cmd = cmd.split()
	if cmd:
		op = parseOp(cmd[0])
		# now validate op
		args = [parseArg(arg) for arg in cmd[1:]]
		# now validate args based on op
		if len(args) != op.nargs:
			raise InputError('Incorrect number of arguments for %s' % (op.__name__))
	else:
		op = None
		args = []
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

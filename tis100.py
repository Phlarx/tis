#!/usr/bin/python3 -bb
#author Derek Anderson

VERSION = '0.0.0'

from argparse import ArgumentParser

###
#  TIS-100 objects
###

class Meta(type):
	def __str__(self):
		return self.__name__
	def __repr__(self):
		return self.__name__

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

class Nodes(object):
	"""The nodes types (first, the abstract)"""
	class abstractNode(object):
		pass

	class compute(abstractNode):
		def __init__(self, code):
			self._code = code
		def __str__(self):
			s = 'T21 Compute '
			if self._code:
				s += '%s...' % (self._code[0],)
			else:
				s += 'empty'
			return s

	class smemory(abstractNode):
		def __init__(self):
			self._stack = []
		def __str__(self):
			s = 'T30 Memory  '
			if self._stack:
				s += '%s...' % (' '.join(map(str, self._stack)))
			else:
				s += 'empty'
			return s

	class damaged(abstractNode):
		def __init__(self):
			pass
		def __str__(self):
			return 'T00 Damaged'

class Operators(object):
	"""All the operators (first, the abstract)"""
	class abstractOp(object, metaclass=Meta):
		nargs = -1

		def __init__(self):
			raise RuntimeError('This class is a namespace -- it should not be instantiated')

		@classmethod
		def run(cls, node, args):
			pass

	class add(abstractOp):
		nargs = 1

		@classmethod
		def run(cls, node, args):
			node['acc'] += node[args[0]]

	class jgz(abstractOp):
		nargs = 1

		@classmethod
		def run(cls, node, args):
			if node['acc'] > 0:
				node.jumpLabel(args[0])

	class jlz(abstractOp):
		nargs = 1

		@classmethod
		def run(cls, node, args):
			if node['acc'] < 0:
				node.jumpLabel(args[0])

	class jmp(abstractOp):
		nargs = 1

		@classmethod
		def run(cls, node, args):
			node.jumpLabel(args[0])

	class mov(abstractOp):
		nargs = 2

		@classmethod
		def run(cls, node, args):
			node[args[1]] = node[args[2]]

	class sav(abstractOp):
		nargs = 0

		@classmethod
		def run(cls, node, args):
			node['bak'] = node['acc']

	class sub(abstractOp):
		nargs = 1

		@classmethod
		def run(cls, node, args):
			node['acc'] -= node[args[0]]

	class swp(abstractOp):
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
	# input defaults to stdin on first of top row; unless none, no input
	# can be defined via -i 'x-xx'? (x)none, (-)stdin (c)har, (s)tdin number, (r)andom number, random (l)ist, random (i)ndex, random (p)ositive, random (n)egative...
	# output defaults to stdout on last of bottom row; unless none, no output
	# can be defined via -o 'xx-x'? (x)none, (-)stdout (c)har, (s)tdout number, (d)rawing...
	# able to use the lua definitions instead? Maybe via Lunatic (https://labix.org/lunatic-python)? (Far in future)
	#
	# what happens when multiple stdins/stdouts?

	parser = ArgumentParser(usage='%(prog)s [options] <tisfile>', conflict_handler='resolve')
	# positional args
	parser.add_argument('tisfile', action='store', type=str, nargs='?', metavar='tisfile', help='A TIS-100 program file.')
	# optional args
	parser.add_argument('-c', '--cols', action='store', type=int, default=4, help='column count')
	parser.add_argument('-h', '--help', action='help', help='Show this help message and exit.')
	parser.add_argument('-i', '--input', action='store', type=str, default='-xxx', help='input layout')
	parser.add_argument('-n', '--nodes', action='store', type=str, default='cccc'*4, help='node layout')
	parser.add_argument('-o', '--output', action='store', type=str, default='xxx-', help='output layout')
	parser.add_argument('-r', '--rows', action='store', type=int, default=3, help='row count')
	parser.add_argument('-V', '--version', action='version', version=('TIS-100 interpreter v'+VERSION), help="Show interpreter's "+
	                                       'version number and exit.')

	args = parser.parse_args()
	print(args)

	assert(args.cols == len(args.input))
	assert(args.cols*args.rows == len(args.nodes))
	assert(args.cols == len(args.output))

	if args.tisfile:
		with open(args.tisfile, 'r') as f:
			prog = f.readlines()
	else:
		parser.print_help()
		exit(0)

	return prog, args

def parseOp(op):
	return getattr(Operators, op.lower())

def parseArg(arg):
	return arg.lower()

def parseLine(line):
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

def parseProg(prog, args):
	index = None
	code = [[] for i in range(args.nodes.count('c'))]
	for line in prog:
		if line[0] == '\n':
			pass # line is empty
		elif line[0] == '@':
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

	print(code)
	nodes = []
	for nodec in args.nodes:
		if nodec == 'c': # T21 compute node
			nodes.append(Nodes.compute(code.pop(0)))
		elif nodec == 'm': # T30 stack memory node
			nodes.append(Nodes.smemory())
		elif nodec == 'd': # damaged node
			nodes.append(Nodes.damaged())
		else:
			raise ValueError("'%s' is not a valid node type." % (nodec))
	return nodes

if __name__ == "__main__":
	prog, args = init()
	for node in parseProg(prog, args):
		print(node)

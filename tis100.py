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

class Registers(object):
	"""The various registers"""
	# todo except acc and bak?

	class abstractRegister(object):
		def read(self):
			# Returns Val() if success, None if fail
			raise RuntimeError('Not yet implemented')
		def write(self, value):
			# Returns True if success, None if fail
			raise RuntimeError('Not yet implemented')

	class acc(abstractRegister):
		def __init__(self):
			self._value = Val(0)
		def read(self):
			return self._value
		def write(self, value):
			self._value = value
			return True
	class bak(abstractRegister):
		def __init__(self):
			self._value = Val(0)
		def read(self):
			raise TISError('Cannot read from BAK')
		def write(self, value):
			raise TISError('Cannot write to BAK')
		def safeRead(self):
			return self._value
		def safeWrite(self, value):
			self._value = value
			return True
	class up(abstractRegister):
		def __init__(self, index):
			self._index = index
		def read(self):
			return Val(0) # attemptRead(...)
		def write(self, value):
			return True # attemptWrite(...)
	class right(abstractRegister):
		def __init__(self, index):
			self._index = index
		def read(self):
			return Val(0) # attemptRead(...)
		def write(self, value):
			return True # attemptWrite(...)
	class down(abstractRegister):
		def __init__(self, index):
			self._index = index
		def read(self):
			return Val(0) # attemptRead(...)
		def write(self, value):
			return True # attemptWrite(...)
	class left(abstractRegister):
		def __init__(self, index):
			self._index = index
		def read(self):
			return Val(0) # attemptRead(...)
		def write(self, value):
			return True # attemptWrite(...)
	class any(abstractRegister):
		def __init__(self, index):
			self._index = index
		def read(self):
			return Val(0) # attemptRead(...)
		def write(self, value):
			return True # attemptWrite(...)
	class nil(abstractRegister):
		def read(self):
			return Val(0)
		def write(self, value):
			return True

class Nodes(object):
	"""The nodes types"""

	# Begin abstract nodes

	class abstractNode(object):
		pass
	class abstractIoNode(abstractNode):
		"""Input/output nodes"""
		pass
	class abstractTisNode(abstractNode):
		"""The T21, T30, or damaged nodes"""
		def __init__(self, index, fakeindex):
			self._index = index
			self._fakeindex = fakeindex
			self._registers = {}
			self._portregisters = {}
			self._registers['up'] = Registers.up(index)
			self._registers['down'] = Registers.down(index)
			self._registers['left'] = Registers.left(index)
			self._registers['right'] = Registers.right(index)
			self._registers['any'] = Registers.any(index)
			self._registers['nil'] = Registers.nil()
			self._registers['acc'] = Registers.acc()
			self._registers['bak'] = Registers.bak()
		def __getitem__(self, key):
			if key in self:
				return self._registers[key].read()
			else:
				try:
					return Val(key)
				except ValueError:
					raise TISError("'%s' is not valid register for a %s node" % (key, self.__class__.__name__))
		def __setitem__(self, key, value):
			if key in self:
				self._registers[key].write(value)
			else:
				raise TISError('key is not valid register for this node type') # todo make better
		def __contains__(self, key):
			return key in self._registers
		def __call__(self, *args, **kwargs):
			return self.run(*args, **kwargs)

	# Begin IO nodes

	class iNull(abstractIoNode):
		pass
	class oNull(abstractIoNode):
		pass
	class iStdin(abstractIoNode):
		pass
	class oStdout(abstractIoNode):
		pass

	# Begin TIS nodes

	class compute(abstractTisNode):
		def __init__(self, index, fakeindex, code):
			super().__init__(index, fakeindex)
			self._code = code
			self._hascode = any([line[1][0] for line in self._code])
			self._state = 'IDLE'
			self._ip = -1 # special not-started ip
		def __str__(self):
			s = '%d(%d) T21 Compute ' % (self._fakeindex, self._index)
			if self._code:
				s += '%s...' % (self._code[0],)
			else:
				s += 'empty'
			return s

		def run(self):
			if not self._hascode:
				return
			else:
				# todo how to handle blocked reads/writes?m try/catch would be good...
				self._state = 'RUNN' # todo what are the state names?
				while True:
					self._ip = (self._ip + 1) % len(self._code)
					instr = self._code[self._ip][1]
					if instr[0]:
						break
				print('Node %d(%d), executing %s %s' % (self._fakeindex, self._index, instr[0], ' '.join(instr[1])))
				instr[0](self, instr[1])
				print('  ip: %d, acc: %d, bak: %d' % (self._ip, self['acc'], self._registers['bak'].safeRead()))
		def jumpLabel(self, label):
			# todo are labels case sensitive?
			for i in range(len(self._code)):
				if self._code[0] == label:
					self._ip = i
		def jumpOffset(self, offset):
			# need to decrement, because we already incremented
			self._ip = (self._ip + offset - 1) % len(self._code)
		def save(self):
			# safe access to bak
			self._registers['bak'].safeWrite(self['acc'])
		def swap(self):
			# safe access to bak
			temp = self._registers['bak'].safeRead()
			self._registers['bak'].safeWrite(self['acc'])
			node['acc'] = temp

	class smemory(abstractTisNode):
		def __init__(self, index, fakeindex):
			super().__init__(index, fakeindex)
			self._stack = []
		def __str__(self):
			s = '%d(%d) T30 Memory  ' % (self._fakeindex, self._index)
			if self._stack:
				s += '%s...' % (' '.join(map(str, self._stack)))
			else:
				s += 'empty'
			return s

		def run(self):
			pass

	class damaged(abstractTisNode):
		def __init__(self, index, fakeindex):
			super().__init__(index, fakeindex)
		def __str__(self):
			return '%d(%d) T00 Damaged' % (self._fakeindex, self._index)

		def run(self):
			pass

class Operators(object):
	"""All the operators"""
	class abstractOp(object, metaclass=Meta):
		nargs = -1
		def __new__(cls, *args, **kwargs):
			return cls.run(*args, **kwargs)

	class add(abstractOp):
		nargs = 1

		@classmethod
		def run(cls, node, args):
			node['acc'] += node[args[0]]
	class jez(abstractOp):
		nargs = 1

		@classmethod
		def run(cls, node, args):
			if node['acc'] == 0:
				node.jumpLabel(args[0])
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
	class jnz(abstractOp):
		nargs = 1

		@classmethod
		def run(cls, node, args):
			if node['acc'] != 0:
				node.jumpLabel(args[0])
	class mov(abstractOp):
		nargs = 2

		@classmethod
		def run(cls, node, args):
			node[args[1]] = node[args[0]]
	class sav(abstractOp):
		nargs = 0

		@classmethod
		def run(cls, node, args):
			node.save()
	class sub(abstractOp):
		nargs = 1

		@classmethod
		def run(cls, node, args):
			node['acc'] -= node[args[0]]
	class swp(abstractOp):
		nargs = 0

		@classmethod
		def run(cls, node, args):
			node.swap()

###
#  Parser & init
###

def init():
	# todo:
	# layout defaults to 3 rows 4 cols, all T21 compute nodes
	# can be defined via -n 'ccmcccccdmcc'? (c)ompute, (m)emory, (d)amaged
	# input defaults to stdin on first of top row; unless none, no input
	# can be defined via -i 'x-xx'? (x)none, (-)std(i)n (c)har, (s)tdin number, (r)andom number, random (l)ist, random in(d)ex, random (p)ositive, random (n)egative...
	# output defaults to stdout on last of bottom row; unless none, no output
	# can be defined via -o 'xx-x'? (x)none, (-)std(o)ut (c)har, (s)tdout number, (g)raphic...
	# able to use the lua definitions instead? Maybe via Lunatic (https://labix.org/lunatic-python)? (Far in future)
	#
	# what happens when multiple stdins/stdouts?
	# allow top outputs in non-strict mode?
	# what is the graphical output called?
	# attempt to infer rows/cols from len(input/output),len(nodes)? what happens if not clean?

	parser = ArgumentParser(usage='%(prog)s [options] <tisfile>', conflict_handler='resolve')
	# positional args
	parser.add_argument('tisfile', action='store', type=str, nargs='?', metavar='tisfile', help='A TIS-100 program file.')
	# optional args
	parser.add_argument('-c', '--cols', action='store', type=int, default=4, help='column count')
	parser.add_argument('-h', '--help', action='help', help='Show this help message and exit.')
	parser.add_argument('-i', '--input', action='store', type=str, default='-xxx', help='input layout')
	parser.add_argument('-n', '--nodes', action='store', type=str, default='cccc'*3, help='node layout')
	parser.add_argument('-o', '--output', action='store', type=str, default='xxx-', help='output layout')
	parser.add_argument('-r', '--rows', action='store', type=int, default=3, help='row count')
	parser.add_argument('-V', '--version', action='version', version=('TIS-100 interpreter v'+VERSION), help="Show interpreter's "+
	                                       'version number and exit.')

	args = parser.parse_args()

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
	return arg.strip(',').lower() # todo are labels case sensitive?

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
			raise TISError('Incorrect number of arguments for command %s, expected %d' % (op.__name__, op.nargs))
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
			# raise exception if in strict mode?
			pass # line is ignored, it is not assigned to a T21 node

	nodes = []
	fi = 0
	for i,nodec in enumerate(args.nodes):
		if nodec == 'c': # T21 compute node
			nodes.append(Nodes.compute(i, fi, code[fi]))
			fi += 1
		elif nodec == 'm': # T30 stack memory node
			nodes.append(Nodes.smemory(i, '.'))
		elif nodec == 'd': # damaged node
			nodes.append(Nodes.damaged(i, '.'))
		else:
			raise ValueError("'%s' is not a supported node type." % (nodec))
	return nodes

def run(nodes):
	for t in range(10):
		for node in nodes:
			node()

if __name__ == "__main__":
	prog, args = init()
	nodes = parseProg(prog, args)
	run(nodes)

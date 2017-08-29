#!/usr/bin/python3 -bb
#author Derek Anderson

VERSION = '0.0.1'

import sys
import threading

from argparse import ArgumentParser

"""---------
Notes:

todo List of states (from memory)?: IDLE, RUN, SLP, READ, WRTE

Will need to find a way to prevent deadlocks (timeout is okay I guess...) (maybe loop detect in barrier helper?)

Just use regular events for the registers... then have a single syncho'd outgoing register to hold the value in each node (this may make 'any' more natural to handle)

Multi-source input is multiple line of file

---------"""

BARRIER_TIMEOUT = 2 # seconds
READ_TIMEOUT = 1 # seconds

###
#  TIS-100 objects
###

class TISError(Exception):
	"""An error has occurred which violates the TIS specification"""
	pass
class ReadWaitException(Exception):
	"""An attempt to read cannot be fulfilled yet"""
	pass

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
	"""The nodes types"""

	opposite = {'up':'down',
	            'down':'up',
	            'left':'right',
	            'right':'left'}

	@staticmethod
	def findNeighbors(node):
		neighbors = {} # todo convert to namedtuple
		row = node._index // node._cfg.cols
		col = node._index % node._cfg.cols
		if row > 0:
			neighbors['up'] = node._cfg.lookup(node._index-node._cfg.cols)
		else:
			neighbors['up'] = None
		if row+1 < node._cfg.rows:
			neighbors['down'] = node._cfg.lookup(node._index+node._cfg.cols)
		else:
			neighbors['down'] = None
		if col > 0:
			neighbors['left'] = node._cfg.lookup(node._index-1)
		else:
			neighbors['left'] = None
		if col+1 < node._cfg.cols:
			neighbors['right'] = node._cfg.lookup(node._index+1)
		else:
			neighbors['right'] = None
		return neighbors


	# Begin abstract nodes

	class abstractNode(object):
		"""All node types"""
		def __init__(self, cfg, locks, index):
			self._cfg = cfg
			self._index = index
			self._locks = locks
			self._state = 'STRT'
			self._neighbors = {}
		def run(self):
			self._neighbors = Nodes.findNeighbors(self)
			while True:
				#print('Node %d: Tick barrier has %d+1 of %d parties' % (self._index, self._locks['tick'].n_waiting, self._locks['tick'].parties))
				self._locks['tick'].wait()
				if self._locks['shutdown'].isSet():
					break
				if 'runInternal' in dir(self):
					self.runInternal()
			# todo end-of-tick cleanup?
		def writePort(self, register, value):
			raise TISError("Node %d: Attempted to write, when it can't." % (self._index,))
		def readPort(self, register):
			raise ReadWaitException()

	class abstractIoNode(abstractNode):
		"""Input/output nodes"""
		pass
	class abstractTisNode(abstractNode):
		"""The T21, T30, or damaged nodes"""
		def __init__(self, cfg, locks, index, fakeindex):
			super().__init__(cfg, locks, index)
			self._fakeindex = fakeindex
			self._acc = Val(0)
			self._bak = Val(0)
			self._outLock = threading.Condition(threading.Lock())
			self._outValue = None # guarded by _outLock
			self._outRegister = None # guarded by _outLock
		def __getitem__(self, key):
			print('Node %d: reading from register %s' % (self._index, key))
			try:
				return Val(key)
			except ValueError:
				pass
			if key == 'nil':
				return Val(0)
			elif key == 'acc':
				return self._acc
			elif key == 'bak':
				raise TISError('Node %d: Cannot read from BAK directly' % (self._index))
			elif key in self._neighbors:
				if self._neighbors[key] is not None:
					return self._neighbors[key].readPort(Nodes.opposite[key])
				else:
					raise TISError("Node %d: Tried to read from out-of-bounds register '%s'" % (self._index, key))
			elif key == 'any':
				return Val(0) # todo how to do this?
			else:
				raise TISError("Node %d: '%s' is not valid source register for a %s node" % (self._index, key, self.__class__.__name__))
		def __setitem__(self, key, value):
			print('Node %d: writing to register %s (value %r)' % (self._index, key, value))
			if key == 'nil':
				pass
			elif key == 'acc':
				self._acc = value
			elif key == 'bak':
				raise TISError('Node %d: Cannot write to BAK directly' % (self._index,))
			elif key in self._neighbors:
				if self._neighbors[key] is not None:
					self.writePort(key, value)
				else:
					raise TISError("Node %d: Tried to write to out-of-bounds register '%s'" % (self._index, key))
			elif key == 'any':
				pass # todo how to do this?
			else:
				raise TISError("Node %d: '%s' is not valid source register for a %s node" % (self._index, key, self.__class__.__name__))
		def writePort(self, register, value):
			self._outLock.acquire()
			try:
				assert(self._outValue is None)
				assert(self._outRegister is None)
				self._outValue = value
				self._outRegister = register
				# todo notify any waiting neighbors?
			finally:
				self._outLock.release()
		def readPort(self, register):
			# called by neighbors
			self._outLock.acquire()
			try:
				if self._outRegister == None:
					self._outLock.wait(READ_TIMEOUT) # todo non-blocking wait for notify? have special values for notyet, notset, notyours?
				if self._outRegister == 'any' or self._outRegister == register:
					ret = self._outValue
					self._outValue = None
					self._outRegister = None
					return ret
				else:
					raise ReadWaitException()
			finally:
				self._outLock.release()
		def finalizeTick(self):
			self._outLock.acquire()
			try:
				self._outLock.notify_all()
			finally:
				self._outLock.release()

	# Begin IO nodes

	class iNull(abstractIoNode):
		def __init__(self, cfg, locks, index):
			super().__init__(cfg, locks, index)
			self._locks['idle'].acquire()
	class oNull(abstractIoNode):
		def __init__(self, cfg, locks, index):
			super().__init__(cfg, locks, index)
			self._locks['idle'].acquire()
	class iStdin(abstractIoNode):
		def __init__(self, cfg, locks, index):
			super().__init__(cfg, locks, index)
		def readPort(self, register):
			# called by neighbors
			assert(register == 'down')
			inchar = sys.stdin.buffer.read(1)
			if len(inchar) == 0:
				if self._state != 'IDLE':
					self._locks['idle'].acquire()
					self._state = 'IDLE'
				raise ReadWaitException()
			else:
				self._state = 'RUNN'
				return Val(ord(inchar))
	class oStdout(abstractIoNode):
		def __init__(self, cfg, locks, index):
			super().__init__(cfg, locks, index)
		def runInternal(self):
			try:
				value = self._neighbors['up'].readPort('down')
				if self._state == 'WAIT':
					self._locks['idle'].release()
					self._state = 'RUNN'
				print('Node %d: output received value to output:    ------->    %s' % (self._index, value))
			except ReadWaitException:
				if self._state != 'WAIT':
					self._locks['idle'].acquire()
					self._state = 'WAIT'
				print('Node %d: output node is waiting' % (self._index,))

	# Begin TIS nodes

	class compute(abstractTisNode):
		def __init__(self, cfg, locks, index, fakeindex, code):
			super().__init__(cfg, locks, index, fakeindex)
			self._code = code
			self._hascode = any([line[1][0] for line in self._code])
			self._ip = -1 # special not-started ip
		def __str__(self):
			s = '%d(%d) T21 Compute ' % (self._index, self._fakeindex)
			if self._code:
				s += '%s...' % (self._code[0],)
			else:
				s += 'empty'
			return s

		def runInternal(self):
			if not self._hascode:
				if self._state != 'IDLE':
					if self._state != 'WAIT':
						self._locks['idle'].acquire()
					self._state = 'IDLE'
				return
			else:
				if self._outRegister is not None:
					self._locks['idle'].acquire()
					self._state = 'WAIT'
					return
				if self._state == 'IDLE' or self._state == 'WAIT':
					self._locks['idle'].release()
				while self._state != 'WAIT':
					self._ip = (self._ip + 1) % len(self._code)
					instr = self._code[self._ip][1]
					if instr[0]:
						break
				else:
					instr = self._code[self._ip][1]
				self._state = 'RUNN'
				print('Node %d(%d): executing %s %s' % (self._index, self._fakeindex, instr[0], ' '.join(instr[1])))
				try:
					instr[0](self, instr[1])
				except ReadWaitException:
					# Try again next tick
					self._locks['idle'].acquire()
					self._state = 'WAIT'
				print('Node %d:     ip: %d, acc: %d, bak: %d' % (self._index, self._ip, self._acc, self._bak))
			self.finalizeTick() # let any waiting nodes know we are done
		def jumpLabel(self, label):
			# todo are labels case sensitive?
			for i in range(len(self._code)):
				if self._code[i][0] == label:
					self._ip = i
					break
			else:
				raise TISError('Node %d: Label %s not found' % (self._index, label))
		def jumpOffset(self, offset):
			# need to decrement, because we increment as first step
			self._ip = (self._ip + offset - 1) % len(self._code)
		def save(self):
			# safe access to bak
			self._bak = self._acc
		def swap(self):
			# safe access to bak
			self._acc, self._bak = self._bak, self._acc

	class smemory(abstractTisNode):
		def __init__(self, cfg, locks, index):
			super().__init__(cfg, locks, index, 'm')
			self._stack = []
		def __str__(self):
			s = '%s(%d) T30 Memory  ' % (self._index, self._fakeindex)
			if self._stack:
				s += '%s...' % (' '.join(map(str, self._stack)))
			else:
				s += 'empty'
			return s

	class damaged(abstractTisNode):
		def __init__(self, cfg, locks, index):
			super().__init__(cfg, locks, index, 'd')
		def __str__(self):
			return '%s(%d) T00 Damaged' % (self._index, self._fakeindex)

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
	# Support for if the opts are all in one chunk (e.g. shebang)
	args = [chunk for subl in sys.argv for chunk in subl.split()]

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

	cfg = parser.parse_args(args[1:])
	cfg.rows += 2 # for i/o rows

	assert(cfg.cols == len(cfg.input)) # todo make more dynamic
	assert(cfg.cols == len(cfg.output))
	assert(cfg.cols*cfg.rows == len(cfg.input+cfg.nodes+cfg.output))

	if cfg.tisfile:
		with open(cfg.tisfile, 'r') as f:
			prog = f.readlines()
	else:
		parser.print_help()
		exit(0)

	return prog, cfg

def createTickAction(locks):
	def reportTick():
		# This runs once per tick, before all other activities.
		reportTick.tick += 1
		if reportTick.locks['idle'].acquire(blocking=False):
			reportTick.locks['idle'].release()
			print('Starting tick %d!' % (reportTick.tick,))
		else:
			# All threads are idle. Initiate shutdown.
			reportTick.locks['shutdown'].set()
			print('All threads stopped, shutting down')
	reportTick.tick = -1
	reportTick.locks = locks
	return reportTick

def parseOp(op):
	return getattr(Operators, op.lower())

def parseArg(arg):
	return arg.strip(',').lower()

# todo handle line modifiers: @, !..., where do they live, what do they mean, etc?
def parseLine(line):
	# now check if line is too long
	code, _, comment = line.partition('#')
	comment = comment.strip()
	# now check if line w/o comment is too long
	label, _, cmd = tuple(code.rpartition(':'))
	label = label.lower()
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

def parseProg(prog, cfg):
	index = None
	code = [[] for i in range(cfg.nodes.count('c'))]
	for line in prog: # todo this whole thing here:
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

	# Create tick barrier, shutdown event, idle counter
	print('Barrier will wait for %d nodes, plus master' % (cfg.cols*cfg.rows,))
	locks = {}
	locks['idle'] = threading.BoundedSemaphore(cfg.cols*cfg.rows)
	locks['shutdown'] = threading.Event()
	locks['tick'] = threading.Barrier(cfg.cols*cfg.rows + 1, action=createTickAction(locks), timeout=BARRIER_TIMEOUT)

	nodes = []
	fi = 0 # fake index, used only by compute nodes, matches what's in program file
	assert(cfg.cols*cfg.rows == len(cfg.input+cfg.nodes+cfg.output))
	for row in range(cfg.rows):
		for col in range(cfg.cols):
			i = row*cfg.cols + col
			nodec = (cfg.input+cfg.nodes+cfg.output)[i]

			if row == 0: # input row
				if nodec == 'x': # Null i node
					nodes.append(Nodes.iNull(cfg, locks, i))
				elif nodec == '-': # Std i node
					nodes.append(Nodes.iStdin(cfg, locks, i))
				else:
					raise ValueError("'%s' is not a supported input node type." % (nodec))
			elif row == cfg.rows-1: # output row
				if nodec == 'x': # Null o node
					nodes.append(Nodes.oNull(cfg, locks, i))
				elif nodec == '-': # Std o node
					nodes.append(Nodes.oStdout(cfg, locks, i))
				else:
					raise ValueError("'%s' is not a supported output node type." % (nodec))
			else: # body
				if nodec == 'c': # T21 compute node
					nodes.append(Nodes.compute(cfg, locks, i, fi, code[fi]))
					fi += 1
				elif nodec == 'd': # damaged node
					nodes.append(Nodes.damaged(cfg, locks, i))
				elif nodec == 'm': # T30 stack memory node
					nodes.append(Nodes.smemory(cfg, locks, i))
				else:
					raise ValueError("'%s' is not a supported TIS node type." % (nodec))
	cfg.lookup = lambda idx: nodes[idx]
	return nodes, locks

def start(nodes):
	threads = set()
	for node in nodes:
		thr = threading.Thread(target=node.run, name='Node-{i}'.format(i=node._index))
		thr.start()
		threads.add(thr)
	return threads

if __name__ == "__main__":
	prog, cfg = init()
	nodes, locks = parseProg(prog, cfg)
	print('Number of nodes: %d' % (len(nodes),))
	print('List of nodes: %r' % (nodes,))
	print('List of locks: %r' % (locks,))
	threads = start(nodes)
	print('Active threads: %d' % (threading.active_count(),))
	interrupted = False
	for i in range(500):
		try:
			locks['tick'].wait()
			if locks['shutdown'].isSet():
				break
		except KeyboardInterrupt:
			if interrupted:
				raise
			else:
				interrupted = True
				locks['shutdown'].set()
				print('Shutting down...')
	for thr in threads:
		thr.join(5)

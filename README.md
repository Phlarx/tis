# TIS-100
A TIS-100 emulator, for the wonderful Zachtronics game of the same name. Uses TIS-100 save file format for code; the layout/specification is a proprietary format.

None of the other emulators available did quite what I wanted, so I'm going to try my hand at making my own.

My goals with this emulator are not to re-implement the game, but to mold this design into something more like a niche, but usable programming language.
There is no verification of outputs against a predefined list; that is the duty of the programmer.
The variety of input and output styles will be expanded from the simple number lists and graphical display of the game, including ASCII input and output.
The layout and arrangement of nodes will be modifiable, and (in the long term) more node types will be added, such as a RAM node.

This is all very WIP, so a bunch of things aren't implemented. However, the instruction set should be functional
and complete. Also no real documentation yet, sorry. It's on my short list. Draft below.

===

(useful links for dev'ment:)
http://www.zachtronics.com/images/TIS-100P%20Reference%20Manual.pdf
https://alandesmet.github.io/TIS-100-Hackers-Guide/

===

## TIS Assembly Language

### The official manual
[The official TIS-100 manual is available from the Zachtronics website.](http://www.zachtronics.com/images/TIS-100P%20Reference%20Manual.pdf)
If you are new to TIS, this is the best place to start whether you intend to play the game, or use this emulator.

### Differences, deviations, addendums and errata
(additional details, especially where this deviates from the maunal, and details on how the save file works)
Deviations between the manual and this emulator include:
- HCF still exits the system immediately, but the emulator attempts a clean exit.

One thing to be aware of is that, like the game, compute nodes are numbered left to right, top to bottom, indexed from zero to n-1, where n is the number of compute nodes. Not all nodes must be present.
When counting, nodes other than compute nodes are skipped. 

## TIS Configuration

### Components of a TIS:
The majority of a TIS is made of several parts called nodes; each node is of a specific type that does a specific job.
These nodes are arranged in a rectangle, and each node can communicate with its four immediate neighbors. (Border nodes will have fewer neighbors.)

The most common node is a execution, or compute node. This node type can hold up to 15 instructions, and those instructions are run on a loop until termination.
Each node has internal registers to store data, and port registers to communicate with its neighbors. See the documentation in the official manual for more details.

Another node type is the stack memory node. This node can store up to 15 numbers in a stack; this stack may be accessed by all four neighbors. See the documentation in the official manual for more details.

If a node is detected to be damaged, it will be disabled and therefore will not interact with neighboring nodes.

The top row of nodes may have an input pseudo-node as their upward neighbor. These pseudo-nodes provide data from an external source.
See the TIS Input/Output section below for more details on available input pseudo-node types.

Likewise, the bottom row of nodes may have an output pseudo-node as their downward neighbor. These pseudo-nodes provide data to an external destination.
See the TIS Input/Output section below for more details on available output pseudo-node types.

### The default configuration
The default configuration of a TIS is 3 rows of 4 nodes each. All twelve nodes will be configured as compute nodes.
A simple ASCII input from stdin will be made available to the upper-left node, and a simple ASCII output will be made available to the lower-right node.
The configurations encountered in the game differ only slightly to this; exhibiting different node types and input/output style and indexes.

Assuming the TIS assembly code is in a file called 'code.tisasm', this is the simplest method of running the emulator:
```shell
tis code.tisasm
```

### Changing the size of the default layout
If two numbers are provided on the command line, the configuration will be initialized to a different size that corresponds to the given rows and columns.
All other aspects will remain default; all nodes will be compute nodes, and the upper-left input and lower-right output willl remain.

Using these rows and columns arguments might look like this, for two rows and 5 columns:
```shell
tis code.tisasm 2 5
```

### Defining a custom layout
For maximum control, use a layout file or string. A file is recommended, but the layout may be provided as a quoted string instead with the -l flag.

The first three elements of a layout file are required, and all elements in a layout file are separated by whitespace (space, tab, newline), with one exception that will be covered below.

The first two elements are the rows and columns of the layout. The third element is a map describing what type each node is.
A total of rows*columns characters are then read, skipping whitespace (this is the exception to the whitespace-delimiter rule). The valid node types are (case insensitive):
- `C` - compute node; T21 Basic Execution Node
- `M` or `S` - stack node; T30 Stack Memory Node
- `R` - ram node; T31 Random Access Memory Node (Not yet implemented)
- `D` - damaged/disabled node; (Not yet implemented)

A sample layout may be:
```text
5 4
CCSD
CCCC
CCCC
CCCC
CSCC
```
Which is 5 rows and 4 columns with two stack nodes and one disabled node listed.

Following the layout details are (optional) definitions of the various input and output pseudo-nodes. Input nodes start with the token `I<n>` where n is the index. `I0` is the first column, `I3` is the fourth, etc.
Output pseudo-nodes are similar, but start with the token `O<n>` instead. If one of these tokens is encountered, it is assumed that the previous definition is complete. These definitions need not be in any specific order,
and if there are conflicts, the later definitions will override the earlier ones. Be warned: this may leak memory or file handles if not used with care.

A sample layout, including some input/output definitions may look like this:
```text
2 3
CCS
CCC
I0 NUMERIC numbers.txt
O0 NUMERIC - 32
O2 ASCII -
```

The details for each type of input and output pseudo node are described below, in the TIS Input/Output section.

Assuming that the file 'layout.tiscfg' contains the above configuration, the command line might look something like this:
```shell
tis code.tisasm layout.tiscfg
```

Since whitespace is the delimiter, this layout produces the same result:
```text
2 3 CCSCCC I0 NUMERIC numbers.txt O0 NUMERIC - 32 O2 ASCII -
```

It is less readable, but more suitable for using as a string on the command line with the `-l` flag. Note the quotes around the layout string.
```shell
tis code.tisasm -l "2 3 CCSCCC I0 NUMERIC numbers.txt O0 NUMERIC - 32 O2 ASCII -"
```

## TIS Input/Output

(describe the various options for IO, both original and new)

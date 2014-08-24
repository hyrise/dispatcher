

all:
	gcc dispatcher.c -O3 -g -DNDEBUG -o dispatcher
	#gcc dispatcher.c -O0 -g -o dispatcher
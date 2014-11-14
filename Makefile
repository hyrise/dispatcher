

all: dispatcher.c
	gcc dispatcher.c -O3 -g -DNDEBUG -o dispatcher -lpthread

test: unit_tests
	./unit_tests

unit_tests: unit_tests.cpp 
	g++ -std=c++0x unit_tests.cpp gtest/main.cpp gtest/gtest-all.cpp gtest/MinimalistPrinter.cpp  -o unit_tests -lpthread

.PHONY: test
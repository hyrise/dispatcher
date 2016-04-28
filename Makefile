OBJS = Dispatcher.o jsoncpp.o RoundRobinDistributor.o StreamDistributor.o
CXX = g++
CXXFLAGS = -Wall -std=c++11 -g -O3
CFLAGS = -Wall -g -O3
LDLIBS = -lpthread
INCLUDEPATHS = ./jsoncpp

all: start_dispatcher hyrise_mock

start_dispatcher : $(OBJS) http.o main.cpp
	$(CXX) main.cpp $(OBJS) http.o -o start_dispatcher $(CXXFLAGS) $(LDLIBS)

hyrise_mock : http.o hyrise_mock.c
	cc http.o hyrise_mock.c -o hyrise_mock $(CFLAGS) $(LDLIBS)

http.o: http.h http.c
	cc -c http.c $(CFLAGS)

jsoncpp.o : jsoncpp/jsoncpp.cpp jsoncpp/json.h
	$(CXX) -c jsoncpp/jsoncpp.cpp $(CXXFLAGS) -I $(INCLUDEPATHS)

clean:
	rm *.o start_dispatcher hyrise_mock

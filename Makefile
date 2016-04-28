OBJS = http.o Dispatcher.o jsoncpp.o RoundRobinDistributor.o StreamDistributor.o
CXX = g++
CXXFLAGS = -Wall -std=c++11 -g -O3
CFLAGS = -Wall -g -O3
LDLIBS = -lpthread
INCLUDEPATHS = ./jsoncpp

all: start_dispatcher hyrise_mock

start_dispatcher : $(OBJS) main.cpp
	$(CXX) main.cpp $(OBJS) -o start_dispatcher $(CXXFLAGS) $(LDLIBS)

hyrise_mock : http_c.o hyrise_mock.c
	cc http_c.o hyrise_mock.c -o hyrise_mock $(CFLAGS) $(LDLIBS)

jsoncpp.o : jsoncpp/jsoncpp.cpp jsoncpp/json.h
	$(CXX) -c jsoncpp/jsoncpp.cpp $(CXXFLAGS) -I $(INCLUDEPATHS)

clean:
	rm *.o start_dispatcher hyrise_mock

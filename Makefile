OBJS = http.o Dispatcher.o jsoncpp.o RoundRobinDistributor.o StreamDistributor.o
CXX = g++
CXXFLAGS = -Wall -std=c++11 -O3
LDLIBS = -lpthread
INCLUDEPATHS = ./jsoncpp

all: start_dispatcher

start_dispatcher : $(OBJS) main.cpp
	$(CXX) main.cpp $(OBJS) -o start_dispatcher $(CXXFLAGS) $(LDLIBS)

jsoncpp.o : jsoncpp/jsoncpp.cpp jsoncpp/json.h
	$(CXX) -c jsoncpp/jsoncpp.cpp $(CXXFLAGS) -I $(INCLUDEPATHS)

clean:
	rm *.o start_dispatcher

OBJS = http.o Dispatcher.o jsoncpp.o AbstractDistributor.o RoundRobinDistributor.o StreamDistributor.o Host.o
CXX = g++
CXXFLAGS = -Wall -std=c++11 -O3
INCLUDEPATHS = ./jsoncpp

all: start_dispatcher

start_dispatcher : $(OBJS) main.cpp
	$(CXX) -std=c++11 -lpthread main.cpp $(OBJS) -o start_dispatcher


Dispatcher.o : Dispatcher.cpp
	$(CXX) $(CXXFLAGS) -c Dispatcher.cpp

AbstractDistributor.o : AbstractDistributor.cpp
	$(CXX) $(CXXFLAGS) -c AbstractDistributor.cpp

RoundRobinDistributor.o : RoundRobinDistributor.cpp
	$(CXX) $(CXXFLAGS) -c RoundRobinDistributor.cpp

StreamDistributor.o : StreamDistributor.cpp
	$(CXX) $(CXXFLAGS) -c StreamDistributor.cpp

Host.o : Host.h Host.cpp
	$(CXX) $(CXXFLAGS) -c Host.cpp

http.o: http.cpp http.h
	$(CXX) $(CXXFLAGS) -c http.cpp

jsoncpp.o : jsoncpp/jsoncpp.cpp jsoncpp/json.h
	$(CXX) $(CXXFLAGS) -I $(INCLUDEPATHS) -c jsoncpp/jsoncpp.cpp

clean:
	rm *.o start_dispatcher

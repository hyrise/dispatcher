OBJS = Dispatcher.o jsoncpp.o RoundRobinDistributor.o StreamDistributor.o Host.o HttpRequest.o HttpResponse.o
CXX = g++
CXXFLAGS = -Wall -std=c++11 -pthread -O3 -lpthread
INCLUDEPATHS = ./jsoncpp

all: start_dispatcher

start_dispatcher : $(OBJS) main.cpp
	$(CXX) $(CXXFLAGS) main.cpp $(OBJS) -o start_dispatcher

Dispatcher.o : Dispatcher.cpp
	$(CXX) $(CXXFLAGS) -c Dispatcher.cpp


RoundRobinDistributor.o : RoundRobinDistributor.cpp
	$(CXX) $(CXXFLAGS) -c RoundRobinDistributor.cpp

StreamDistributor.o : StreamDistributor.cpp
	$(CXX) $(CXXFLAGS) -c StreamDistributor.cpp

Host.o : Host.h Host.cpp HttpRequest.h HttpResponse.h
	$(CXX) $(CXXFLAGS) -c Host.cpp

HttpRequest.o : HttpRequest.h HttpRequest.cpp
	$(CXX) $(CXXFLAGS) -c HttpRequest.cpp

HttpResponse.o : HttpResponse.h HttpResponse.cpp
	$(CXX) $(CXXFLAGS) -c HttpResponse.cpp

jsoncpp.o : jsoncpp/jsoncpp.cpp jsoncpp/json.h
	$(CXX) $(CXXFLAGS) -I $(INCLUDEPATHS) -c jsoncpp/jsoncpp.cpp

clean:
	rm *.o start_dispatcher

OBJS = Dispatcher.o jsoncpp.o RoundRobinDispatcher.o StreamDispatcher.o Host.o HttpRequest.o HttpResponse.o
CXX = g++
CXXFLAGS = -Wall -std=c++11 -O3
INCLUDEPATHS = ./jsoncpp

all: start_dispatcher

start_dispatcher : $(OBJS) main.cpp
	$(CXX) main.cpp $(OBJS) -o start_dispatcher

Dispatcher.o : Dispatcher.cpp
	$(CXX) $(CXXFLAGS) -c Dispatcher.cpp


RoundRobinDispatcher.o : RoundRobinDispatcher.cpp
	$(CXX) $(CXXFLAGS) -c RoundRobinDispatcher.cpp

StreamDispatcher.o : StreamDispatcher.cpp
	$(CXX) $(CXXFLAGS) -c StreamDispatcher.cpp

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

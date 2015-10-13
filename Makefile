OBJS = dispatcher.o Host.o HttpRequest.o HttpResponse.o StreamDispatcher.o SimpleRoundRobinDispatcher.o jsoncpp.o
CXX = g++
CXXFLAGS = -Wall -c -std=c++11
LFLAGS = -Wall
LDLIBS = -lpthread
INCLUDEPATHS = ./jsoncpp

all: CXXFLAGS += -DNDEBUG -O3
all: dispatcher

debug: CXXFLAGS += -DDEBUG -g -O0
debug: dispatcher

dispatcher : $(OBJS)
	$(CXX) $(LFLAGS) -I $(INCLUDEPATHS) $(OBJS) -o dispatcher $(LDLIBS)

dispatcher.o : dispatcher.cpp Host.h HttpRequest.h jsoncpp/json.h AbstractDispatcher.h SimpleRoundRobinDispatcher.h
	$(CXX) $(CXXFLAGS) dispatcher.cpp

Host.o : Host.h Host.cpp HttpRequest.h HttpResponse.h
	$(CXX) $(CXXFLAGS) Host.cpp

HttpRequest.o : HttpRequest.h HttpRequest.cpp 
	$(CXX) $(CXXFLAGS) HttpRequest.cpp

HttpResponse.o : HttpResponse.h HttpResponse.cpp 
	$(CXX) $(CXXFLAGS) HttpResponse.cpp

SimpleRoundRobinDispatcher.o : SimpleRoundRobinDispatcher.h SimpleRoundRobinDispatcher.cpp AbstractDispatcher.h Host.h HttpRequest.h HttpResponse.h
	$(CXX) $(CXXFLAGS) SimpleRoundRobinDispatcher.cpp

StreamDispatcher.o : StreamDispatcher.h StreamDispatcher.cpp AbstractDispatcher.h Host.h HttpRequest.h HttpResponse.h
	$(CXX) $(CXXFLAGS) StreamDispatcher.cpp

jsoncpp.o : jsoncpp/jsoncpp.cpp jsoncpp/json.h
	$(CXX) -c -std=c++11 -I $(INCLUDEPATHS) jsoncpp/jsoncpp.cpp

clean:
	rm *.o dispatcher

#all: dispatcher.cpp
#	g++ -std=c++11 -I ./jsoncpp dispatcher.cpp Host.cpp HttpRequest.cpp SimpleRoundRobinDispatcher.cpp jsoncpp/jsoncpp.cpp -o dispatcher -lpthread

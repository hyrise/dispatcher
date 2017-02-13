OBJS = Dispatcher.o jsoncpp.o RoundRobinDistributor.o StreamDistributor.o
CXX = g++
CXXFLAGS = -Wall -std=c++11 -g -O3
CFLAGS = -Wall -g -O3
LDLIBS = -lpthread
INCLUDEPATHS = ./jsoncpp

all: start_dispatcher hyrise_mock query_hyrise dict_test simple_dispatcher

simple_dispatcher: http.o simple_dispatcher.c
	cc http.o dict.o simple_dispatcher.c -o simple_dispatcher $(CFLAGS) $(LDLIBS)

start_dispatcher : $(OBJS) http.o main.cpp
	$(CXX) main.cpp $(OBJS) dict.o http.o -o start_dispatcher $(CXXFLAGS) $(LDLIBS)

hyrise_mock : http.o hyrise_mock.c
	cc http.o dict.o hyrise_mock.c -o hyrise_mock $(CFLAGS) $(LDLIBS)

query_hyrise : http.o query_hyrise.o
	cc -D_GNU_SOURCE http.o dict.o query_hyrise.o -o query_hyrise $(CFLAGS) $(LDLIBS) -pg

query_hyrise.o: query_hyrise.c http.h
	cc -D_GNU_SOURCE -c query_hyrise.c $(CFLAGS) -pg

http.o: http.h http.c dict.o
	cc -D_GNU_SOURCE -c http.c $(CFLAGS) -pg

dict.o: dict.h dict.c
	cc -D_GNU_SOURCE -c dict.c $(CFLAGS) -pg

dict_test: dict.o dict_test.c
	cc dict.o dict_test.c -o dict_test $(CFLAGS) $(LDLIBS)

jsoncpp.o : jsoncpp/jsoncpp.cpp jsoncpp/json.h
	$(CXX) -c jsoncpp/jsoncpp.cpp $(CXXFLAGS) -I $(INCLUDEPATHS)

clean:
	rm *.o start_dispatcher hyrise_mock query_hyrise dict_test simple_dispatcher

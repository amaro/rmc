CXXFLAGS  := -Wall -Werror -g -std=c++17 -O2
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := rmcclient rmcserver

all: $(APPS)

rmcclient: rmcclient.o rdmapeer.o client.o
	$(CXX) -o $@ $^ ${LDLIBS}

rmcserver: rmcserver.o rdmapeer.o server.o
	$(CXX) -o $@ $^ ${LDLIBS}

.PHONY: clean
clean:
	rm -f *.o $(APPS)

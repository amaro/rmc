CXXFLAGS  := -Wall -Werror -g -std=c++17 -O2
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := client nicserver

all: $(APPS)

client: client.o rdmaclient.o rdmapeer.o
	$(CXX) -o $@ $^ ${LDLIBS}

nicserver: nicserver.o rdmaserver.o rdmapeer.o
	$(CXX) -o $@ $^ ${LDLIBS}

.PHONY: clean
clean:
	rm -f *.o $(APPS)

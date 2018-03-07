all: source

CXX = g++
CFLAGS = -g -O0 -Wall -pedantic -std=c++11

source: Source.cpp
	$(CXX) $(CFLAGS) -c Source.cpp -o source.o
	$(CXX) $(CFLAGS) -o source source.o $(LDFLAGS) $(LIBS)

clean:
	rm -f source.o source


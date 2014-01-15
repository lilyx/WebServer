CXX = g++ -fPIC
NETLIBS= -lnsl
CC= gcc -fPIC

all: daytime-server myhttpd use-dlopen hello.so jj-mod.so

daytime-server : daytime-server.o
	$(CXX) -o $@ $@.o $(NETLIBS)

myhttpd: myhttpd.o
	$(CXX) -o $@ $@.o $(NETLIBS) -ldl -lpthread

use-dlopen: use-dlopen.o
	$(CXX) -o $@ $@.o $(NETLIBS) -ldl

hello.so: hello.o
	ld -G -o hello.so hello.o

jj-mod.o: jj-mod.c 
	$(CC) -c jj-mod.c

util.o: util.c 
	$(CC) -c util.c

jj-mod.so: jj-mod.o util.o 
	ld -G -o jj-mod.so jj-mod.o util.o

%.o: %.cpp
	@echo 'Building $@ from $<'
	$(CXX) -o $@ -c -I. $<

clean:
	rm -f *.o use-dlopen
	rm -f *.o daytime-server
	rm -f *.o myhttpd


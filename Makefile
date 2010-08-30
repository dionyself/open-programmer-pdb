CC = gcc
#CFLAGS = -w -O3 -s
CFLAGS = -w
OBJECTS = pdb.o

all: pdb

op : $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o pdb

%.o : %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f pdb $(OBJECTS)


.PHONY: clean

prefix	:= /usr/local
    
install: pdb
	test -d $(prefix) || mkdir $(prefix)
	test -d $(prefix)/bin || mkdir $(prefix)/bin
	install -m 0755 pdb $(prefix)/bin;
	
.PHONY: install

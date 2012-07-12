JOINER = flvjoin
PARSER = flvparse
all: $(JOINER) $(PARSER)

PREFIX = /usr/local

CC = gcc
CFLAGS = -O2 -Wall
LDFLAGS = -s

DEPS = flvjoin.h data_conv.h

JOINER_OBJS = flvjoin.o data_conv.o metadata.o
PARSER_OBJS = flvparse.o data_conv.o

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(JOINER): $(JOINER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^	

$(PARSER): $(PARSER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^	

install: $(JOINER) $(PARSER)
	-mkdir -p $(PREFIX)/bin
	install $(JOINER) $(PREFIX)/bin 
	install $(PARSER) $(PREFIX)/bin 

clean:
	rm -f $(JOINER_OBJS) $(PARSER_OBJS) $(JOINER) $(PARSER)

PROG=drone_con fat_test

LDFLAGS?=
CFLAGS?=
CFLAGS+=-I./ -Wall -Werror

all: $(PROG) 

drone_con: drone_con.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

fat_test: fat_test.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

clean:
	rm -f *.o $(PROG)

%.o: %.c
	$(CC) $(CFLAGS) -c  $^ -o $@

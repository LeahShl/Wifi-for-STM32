CC= gcc
CFLAGS= -Wall -Wextra -g

mthw_tester: main.o tests_db.o
	$(CC) $(CFLAGS) main.o tests_db.o -o mthw_tester -lsqlite3

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

tests_db.o: tests_db.c tests_db.h
	$(CC) $(CFLAGS) -c tests_db.c

PHONY: clean

clean:
	rm *.o

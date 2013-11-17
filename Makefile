all: main.c
	gcc -o nav main.c -lgps -lcurl -I.


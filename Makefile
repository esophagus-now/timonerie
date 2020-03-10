main: main.c queue.h queue.c textio.h textio.c
	gcc -g -o main -Wall -fno-diagnostics-show-caret main.c queue.c textio.c -lpthread

clean:
	rm -rf main
	rm -rf *.o

main: main.c queue.h queue.c textio.h textio.c dbg_guv.h dbg_guv.c dbg_cmd.h dbg_cmd.c
	gcc -g -o main -Wall -fno-diagnostics-show-caret main.c queue.c textio.c dbg_guv.c dbg_cmd.c -lpthread -lreadline

clean:
	rm -rf main
	rm -rf *.o

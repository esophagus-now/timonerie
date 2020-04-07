# Yeah the Makefile is gross. What's it to you??

main: main.c queue.h queue.c textio.h textio.c dbg_guv.h dbg_guv.c dbg_cmd.h dbg_cmd.c pollfd_array.h pollfd_array.c symtab.h symtab.c twm.h twm.c
	gcc -g -o main -Wall -fno-diagnostics-show-caret main.c queue.c textio.c dbg_guv.c dbg_cmd.c pollfd_array.c symtab.c twm.c -lpthread -lreadline

fake_dbg_guv: fake_dbg_guv.c
	gcc -g -Wall -fno-diagnostics-show-caret -o fake_dbg_guv{,.c} -lpthread

clean:
	rm -rf main
	rm -rf *.o

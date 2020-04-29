# Yeah the Makefile is gross. What's it to you??

main: main.c textio.h textio.c dbg_guv.h dbg_guv.c dbg_cmd.h dbg_cmd.c symtab.h symtab.c twm.h twm.c timonier.h timonier.c
	gcc -g -o main -Wall -Wno-cpp -fno-diagnostics-show-caret main.c textio.c dbg_guv.c dbg_cmd.c symtab.c twm.c timonier.c -lreadline -levent

fake_dbg_guv: fake_dbg_guv.c
	gcc -g -Wall -fno-diagnostics-show-caret -o fake_dbg_guv{,.c} -lpthread

clean:
	rm -rf main
	rm -rf *.o

paren: main.c
	gcc -O3 -march=native \
		main.c -c -o paren.o -Wall -Wextra
	ld -I /lib64/ld-linux-x86-64.so.2 paren.o -o paren -lc

validate: validate.c
	gcc -O3 -march=native validate.c -o validate -Wall -Wextra

debug: main.c
	gcc -DDEBUG -O0 -g -march=native main.c -Wall -Wextra \
		-Wno-unused-function -o paren

tspeed: paren
	timeout 15 taskset 1 ./paren | taskset 2 pv -ra > /dev/null

tperf: paren
	perf stat timeout 15 taskset 1 ./paren | taskset 2 pv -q > /dev/null

tvalid: validate paren
	./paren | ./validate

clean:
	rm -f ./paren
	rm -f ./validate



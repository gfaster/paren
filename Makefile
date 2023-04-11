paren: main.c
	gcc -O3 -march=native -fwhole-program  \
		main.c -o paren -Wall -Wextra

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



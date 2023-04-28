paren: main.c
	gcc -O3 -march=native -mtune=native \
		main.c -c -o paren.o -Wall -Wextra -ffat-lto-objects -flto
	ld -flto -I /lib64/ld-linux-x86-64.so.2 paren.o -o paren -lc

validate: validate.c
	gcc -O3 -march=native validate.c -o validate -Wall -Wextra

debug: main.c
	gcc -DDEBUG -O0 -c -g -march=native main.c -Wall -Wextra \
		-Wno-unused-function -o paren.o
	ld -I /lib64/ld-linux-x86-64.so.2 paren.o -o paren -lc

tspeed: paren
	timeout 15 taskset 1 ./paren | taskset 2 pv -ra > /dev/null

tperfstat: paren
	perf stat -e branches -e branch-misses -e cache-misses \
	-e cache-references \
	-e cycles -e alignment-faults -e major-faults -e minor-faults \
	-e dTLB-loads -e dTLB-load-misses -e dTLB-stores -e dTLB-store-misses \
	timeout 150 sh run.sh

tperf: paren
	perf record -a sh run.sh

tvalid: validate paren
	./paren | ./validate

clean:
	rm -f ./paren
	rm -f ./paren.o
	rm -f ./validate



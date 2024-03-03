CFLAGS=-Wall -Wextra -march=native -Wno-unused-function -fno-strict-aliasing
OPTFLAGS=-ffat-lto-objects -flto -mtune=native

paren: main.c
	gcc -O3 $(CFLAGS) $(OPTFLAGS) main.c -c -o paren.o
	ld -flto -I /lib64/ld-linux-x86-64.so.2 paren.o -o paren -lc

validate: validate.c
	gcc -O3 -march=native validate.c -o validate -Wall -Wextra

debug: main.c
	gcc -DDEBUG -O0 -c -g $(CFLAGS) main.c -o paren.o
	ld -I /lib64/ld-linux-x86-64.so.2 paren.o -o paren -lc

tspeed: paren
	timeout 15 taskset 1 ./paren | taskset 2 pv -ra > /dev/null

tperfstat: paren
	perf stat -e branches -e branch-misses -e cache-misses \
	-e cache-references \
	-e cycles -e alignment-faults -e major-faults \
	-e cpu_clk_unhalted.thread \
	-e cycle_activity.stalls_l2_miss \
	-e cycle_activity.stalls_mem_any \
	-e cycle_activity.stalls_total \
	timeout 30 sh run.sh
	# -e frontend_retired.latency_ge_16 \
	# -e frontend_retired.latency_ge_4 \
	# -e cycle_activity.stalls_l3_miss \
	# -e cycle_activity.stalls_l2_miss \
	# -e cycle_activity.stalls_l1d_miss \
	# -e l2_rqsts.miss \
	# -e l2_rqsts.references \

tperf: paren
	perf record -e l2_rqsts.miss sh run.sh

tvalid: validate paren
	./paren | ./validate

clean:
	rm -f ./paren
	rm -f ./paren.o
	rm -f ./validate



CFLAGS+=-Wall -Wextra -march=native -Wno-unused-function -fno-strict-aliasing -no-pie
OPTFLAGS=-mtune=native

paren: main.c unrolled.generated.h
	gcc -g -O3 $(CFLAGS) $(OPTFLAGS) main.c -c -o paren.o
	ld -I /lib64/ld-linux-x86-64.so.2 paren.o -o paren -lc

validate: validate.c
	gcc -O3 -march=native validate.c -o validate -Wall -Wextra

debug: main.c
	gcc -DDEBUG -O0 -c -g $(CFLAGS) main.c -o paren.o
	ld -I /lib64/ld-linux-x86-64.so.2 paren.o -o paren -lc

unrolled.generated.h: unroll.py
	python3 ./unroll.py

main.c: unrolled.generated.h

tspeed: paren
	nice -20 timeout 15 taskset 1 ./paren | nice -20 taskset 2 pv -ra -B 256K > /dev/null

tperfstat: paren
	perf stat -D1000 -e cs \
	-e LLC-loads -e LLC-stores -e ld_blocks.store_forward \
	timeout 30 ./paren | pv -q -B 256k > /dev/null
	# --all-user \
	# -e cycles \
	# -e exe_activity.bound_on_stores \
	# -e cycle_activity.stalls_l2_miss -e cycle_activity.stalls_mem_any -e cycle_activity.stalls_total \
	# -e exe_activity.1_ports_util -e exe_activity.2_ports_util -e exe_activity.3_ports_util -e exe_activity.4_ports_util \
	# -e branches -e branch-misses -e cache-misses \
	# -e alignment-faults -e major-faults \
	# -e frontend_retired.latency_ge_16 \
	# -e frontend_retired.latency_ge_4 \
	# -e cycle_activity.stalls_l3_miss \
	# -e cycle_activity.stalls_l2_miss \
	# -e cycle_activity.stalls_l1d_miss \
	# -e l2_rqsts.miss \
	# -e l2_rqsts.references \

tperf: paren
	perf record -e exe_activity.1_ports_util sh run.sh

tvalid: validate paren
	./paren | ./validate

clean:
	rm -f ./paren
	rm -f ./paren.o
	rm -f ./validate
	rm -f *.generated.h



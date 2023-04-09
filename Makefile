paren: main.c
	gcc -O3 -march=native -fwhole-program  \
		main.c -o paren -Wall -Wextra

validate: validate.c
	gcc -O3 -march=native validate.c -o validate -Wall -Wextra

tspeed: paren
	timeout 15 taskset 1 ./paren | taskset 2 pv -ra > /dev/null

tvalid: validate paren
	./paren | ./validate

clean:
	rm -f ./paren
	rm -f ./validate



paren: main.c
	gcc -O3 -march=native main.c -o paren -Wall -Wextra

validate: validate.c
	gcc -O3 -march=native validate.c -o validate -Wall -Wextra

tspeed: paren
	timeout 15 ./paren | pv -ra > /dev/null

tvalid: validate paren
	./paren | ./validate

clean:
	rm -f ./paren



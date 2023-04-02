paren: main.c
	gcc -O3 -march=native main.c -o paren -Wall -Wextra

tspeed: paren
	timeout 15 ./paren | pv -ra > /dev/null

clean:
	rm -f ./paren



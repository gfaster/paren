paren: main.c
	gcc -O3 main.c -o paren

tspeed: paren
	timeout 15 ./paren | pv -ra > /dev/null

clean:
	rm -f ./paren



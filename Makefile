paren: main.c
	gcc -O3 main.c -o paren

tspeed: paren
	./paren | pv -ra > /dev/null



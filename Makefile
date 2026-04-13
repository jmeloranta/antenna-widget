ant: ant.c
	gcc ant.c -o ant -march=native `pkg-config --cflags --libs gtk+-3.0`

clean:
	rm ant *~

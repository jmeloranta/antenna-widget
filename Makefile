ant: ant.c
	gcc ant.c -o ant -march=native `pkg-config --cflags --libs gtk4`

clean:
	rm ant *~

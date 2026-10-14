all: ant rs

ant: ant.c
	gcc ant.c -o ant -march=native `pkg-config --cflags --libs gtk4`

rs: rs.c
	gcc rs.c -o rs -march=native -lbluetooth

clean:
	rm ant *~

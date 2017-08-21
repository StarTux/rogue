rogue: rogue.o
	cc -g3 -o rogue bin/rogue.o -lncurses
rogue.o: src/rogue.c
	cc -g3 -c src/rogue.c -o bin/rogue.o
clean:
	rm -vf bin/*.o
	rm -vf rogue

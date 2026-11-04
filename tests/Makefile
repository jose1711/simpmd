.PHONY:	all check clean

CC = g++
CC_OPTS = -std=c++11 -O1 -I../src
LD_LIBS = -lSDL2 -lpthread

check: tape_test
	@d=`mktemp -d` && SDL_VIDEODRIVER=dummy ./tape_test "$$d"; r=$$?; rm -rf "$$d"; exit $$r

tape_test: tape_test.cpp ../src/sim_tape.c ../src/sim_common.h
	$(CC) $(CC_OPTS) -x c++ ../src/sim_tape.c -x c++ tape_test.cpp -o $@ $(LD) $(LD_LIBS)

clean:
	rm -f tape_test

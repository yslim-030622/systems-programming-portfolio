CC      = gcc
CFLAGS  = -O0 -g -std=c11 -Wall -Wextra -pthread
LDFLAGS = -pthread

INCLUDE = -Iinclude

SRC     = src/sync_utils.c \
          src/readers_writers.c \
          src/bounded_buffer.c \
          src/main.c

TESTSRC = ../tests/test_scenario.c \
          src/sync_utils.c \
          src/readers_writers.c \
          src/bounded_buffer.c

BBTEST  = ../tests/test_bounded_buffer.c \
          src/sync_utils.c \
          src/bounded_buffer.c

RWMULTI = ../tests/test_rw_sequences.c \
          src/sync_utils.c \
          src/readers_writers.c

RWSTRESS = ../tests/test_rw_stress.c \
           src/sync_utils.c \
           src/readers_writers.c

BBSEQ = ../tests/test_bb_sequences.c \
        src/sync_utils.c \
        src/bounded_buffer.c

BBSTRESS = ../tests/test_bb_stress.c \
           src/sync_utils.c \
           src/bounded_buffer.c

OBJ     = $(SRC:.c=.o)

all: conference_sim

conference_sim: $(SRC)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $(SRC) $(LDFLAGS)

test_scenario: $(TESTSRC)
	$(CC) $(CFLAGS) $(INCLUDE) -o ../tests/$@ $(TESTSRC) $(LDFLAGS)

test_bounded_buffer: $(BBTEST)
	$(CC) $(CFLAGS) $(INCLUDE) -o ../tests/$@ $(BBTEST) $(LDFLAGS)

test_rw_sequences: $(RWMULTI)
	$(CC) $(CFLAGS) $(INCLUDE) -o ../tests/$@ $(RWMULTI) $(LDFLAGS)

test_rw_stress: $(RWSTRESS)
	$(CC) $(CFLAGS) $(INCLUDE) -o ../tests/$@ $(RWSTRESS) $(LDFLAGS)

test_bb_sequences: $(BBSEQ)
	$(CC) $(CFLAGS) $(INCLUDE) -o ../tests/$@ $(BBSEQ) $(LDFLAGS)

test_bb_stress: $(BBSTRESS)
	$(CC) $(CFLAGS) $(INCLUDE) -o ../tests/$@ $(BBSTRESS) $(LDFLAGS)

clean:
	rm -f conference_sim $(OBJ)
	rm -rf *.dSYM
	rm -f ../tests/test_scenario ../tests/test_bounded_buffer ../tests/test_rw_sequences ../tests/test_rw_stress ../tests/test_bb_sequences ../tests/test_bb_stress
	rm -rf ../tests/*.dSYM

.PHONY: all clean test_scenario test_bounded_buffer test_rw_sequences test_rw_stress test_bb_sequences test_bb_stress

CFLAGS := -std=c99 -Wall -Wextra -Werror -Wpedantic -Og -g
CC := cc $(CFLAGS)

TARGETS := auto-power-profile
COMMON_HEADERS := log.h

all: $(TARGETS)

%.o: %.c %.h
	$(CC) -c -o $@ $<

%.o: %.c $(COMMON_HEADERS)
	$(CC) -c -o $@ $<

auto-power-profile: auto-power-profile.o log.o
	$(CC) -o $@ $^ -ldbus-1

run: auto-power-profile
	./auto-power-profile

clean:
	-rm *.o
	-rm $(TARGETS)

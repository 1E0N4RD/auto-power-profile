CFLAGS := -std=c99 -Wall -Wextra -Werror -Wpedantic -Og -g
CFLAGS += $(shell pkg-config --cflags --libs dbus-1)
CC += $(CFLAGS)

TARGETS := auto-power-profile
COMMON_HEADERS := log.h

all: $(TARGETS)

.PHONY:
	install

%.o: %.c %.h
	$(CC) -c -o $@ $<

%.o: %.c $(COMMON_HEADERS)
	$(CC) -c -o $@ $<

auto-power-profile: auto-power-profile.o log.o
	$(CC) -o $@ $^ -ldbus-1

run: auto-power-profile
	./auto-power-profile

install: auto-power-profile
	mkdir -p $(PREFIX)/bin
	cp -t $(PREFIX)/bin auto-power-profile

clean:
	-rm *.o
	-rm $(TARGETS)

CC          := gcc
CFLAGS      := -Wall -Wextra -O2
LIBS        := -lncursesw -lmenuw

TARGET      := ffm

SRCS        := $(wildcard *.c)
OBJS        := $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
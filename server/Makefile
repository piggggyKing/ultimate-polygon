CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2
LDFLAGS := -lcsfml-graphics -lcsfml-window -lcsfml-system

TARGET  := server
SRCS	:= main.c
OBJS	:= $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2
LDFLAGS := -lcsfml-graphics -lcsfml-window -lcsfml-system -lm

TARGET  := client
SRCS	:= main.c TypeText.c Button.c Room.c PlayerSlot.c
OBJS	:= $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
INC     = -I .

TARGET  = libm17.so

CFLAGS  = $(INC) -fPIC
LDFLAGS = -shared -lm
SRCS    = $(wildcard *.c) $(wildcard */*.c)
OBJS    = $(SRCS:.c=.o)

all: $(TARGET) clean

clean:
	rm -f $(OBJS)

fclean:
	rm -f $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

.PHONY: all clean fclean

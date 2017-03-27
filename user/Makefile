CC=gcc
CFLAGS=-I.
DEPS = spifpga_user.h
OBJ = spifpga_user.o main.o 

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

spifpga_user: $(OBJ)
	gcc -o $@ $^ $(CFLAGS)

CC=gcc
CFLAGS=-I.
DEPS = spifpga_user.h
OBJ = spifpga_user.o main.o 
MAIN = spifpga_user

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(MAIN): $(OBJ)
	gcc -o $@ $^ $(CFLAGS)

.PHONY : clean

clean :
	-rm -f $(OBJ) *~ $(MAIN)

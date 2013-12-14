
GCC = gcc
FLAGS = -Wall

all:
	${GCC} client.c -o client ${FLAGS} -lncurses -lpthread
	${GCC} server.c -o server ${FLAGS}


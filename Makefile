CC=g++  
CXXFLAGS = -std=c++0x
CFLAGS=-I

server: ./chat_server.o
	$(CC) -o ./server ./chat_server.cpp --std=c++14 -pthread 
	rm -f ./chat_server.o

client: ./chat_client
	$(CC) -o ./client ./chat_client.cpp --std=c++14 -pthread 
	rm -f ./chat_client.o

clean:
	rm -f ./server
	rm -f ./client
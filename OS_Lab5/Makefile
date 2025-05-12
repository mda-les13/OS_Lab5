CXX = g++

CXXFLAGS = -Wall -Wextra -std=c++98 -D_CRT_SECURE_NO_WARNINGS

TARGETS = Client.exe Server.exe

SRC_CLIENT = Client/Client.cpp
SRC_SERVER = Server/Server.cpp

OBJ_CLIENT = Client/Client.o
OBJ_SERVER = Server/Server.o

all: $(TARGETS)

Client.exe: $(OBJ_CLIENT)
	$(CXX) $(CXXFLAGS) -o $@ $^

Server.exe: $(OBJ_SERVER)
	$(CXX) $(CXXFLAGS) -o $@ $^

Client/Client.o: $(SRC_CLIENT)
	$(CXX) $(CXXFLAGS) -c $< -o $@

Server/Server.o: $(SRC_SERVER)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Очистка
clean:
	rm -f $(OBJ_CLIENT) $(OBJ_SERVER) $(TARGETS)

.PHONY: all clean
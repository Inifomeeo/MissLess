CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -g

SERVER_SRC = server.cpp
CLIENT_SRC = client.cpp
SERVER_OBJ = $(SERVER_SRC:.cpp=.o)
CLIENT_OBJ = $(CLIENT_SRC:.cpp=.o)

SERVER_BIN = server
CLIENT_BIN = client

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_OBJ)
	$(CXX) $(SERVER_OBJ) -o $(SERVER_BIN)

$(CLIENT_BIN): $(CLIENT_OBJ)
	$(CXX) $(CLIENT_OBJ) -o $(CLIENT_BIN)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER_OBJ) $(CLIENT_OBJ) $(SERVER_BIN) $(CLIENT_BIN)

.PHONY: all clean
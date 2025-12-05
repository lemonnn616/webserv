NAME := webserv

SRCS := \
	src/main.cpp \
	src/core/CoreServer.cpp \
	src/core/EventLoop.cpp \
	src/core/Client.cpp \
	src/core/Logger.cpp

OBJ_DIR := obj

OBJECTS := $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

INCLUDES := -Isrc

CXX := c++

CXXFLAGS := -Wall -Wextra -Werror -std=c++17 -O2 $(INCLUDES)

LDFLAGS :=

all: $(NAME)

$(NAME): $(OBJECTS)
	@echo "[Link] $(NAME)…"
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LDFLAGS) -o $@
	@echo "[Success] $(NAME) created!"

$(OBJ_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@echo "[Compile] $< -> $@"
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@echo "[Clean] Removing object files…"
	rm -rf $(OBJ_DIR)
	@echo "[Clean] Done."

fclean: clean
	@echo "[Fclean] Removing binary $(NAME)…"
	rm -f $(NAME)
	@echo "[Fclean] Done."

re: fclean all

.PHONY: all clean fclean re
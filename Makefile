NAME := webserv

# -------- Modules --------
MODULES := main core http utils config

# -------- Directories --------
DIR_main := src
DIR_core := src/core
DIR_http := src/http
DIR_utils := src/utils
DIR_config := config

# -------- Sources --------
SRC_main := main.cpp
SRC_core := CoreServer.cpp EventLoop.cpp Client.cpp Logger.cpp

SRC_http := \
	HttpHandler.cpp \
	HttpParser.cpp \
	HttpRouter.cpp \
	HttpResponse.cpp \
	AutoIndex.cpp \
	ErrorPage.cpp

SRC_utils := FileUtils.cpp
SRC_config := ConfigParser.cpp

# -------- Build lists --------
SRCS := $(foreach m,$(MODULES),$(addprefix $(DIR_$(m))/,$(SRC_$(m))))
OBJ_DIR := obj

OBJECTS := \
	$(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(filter src/%.cpp,$(SRCS))) \
	$(patsubst config/%.cpp,$(OBJ_DIR)/config/%.o,$(filter config/%.cpp,$(SRCS)))

# -------- Compiler --------
CXX := c++
CXXFLAGS := -Wall -Wextra -Werror -std=c++17 -O2
INCLUDES := -Isrc -Iconfig

# -------- Rules --------
all: $(NAME)

$(NAME): $(OBJECTS)
	@echo "[Link] $(NAME)…"
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@
	@echo "[Success] $(NAME) created!"

$(OBJ_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@echo "[Compile] $< -> $@"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/config/%.o: config/%.cpp
	@mkdir -p $(dir $@)
	@echo "[Compile] $< -> $@"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@echo "[Clean] Removing object files…"
	@rm -rf $(OBJ_DIR)
	@echo "[Clean] Done."

fclean: clean
	@echo "[Fclean] Removing binary $(NAME)…"
	@rm -f $(NAME)
	@echo "[Fclean] Done."

re: fclean all

.PHONY: all clean fclean re

# Host unit tests for margin's pure C++ libraries (protocol framing, TLV, chat model, Bible).
# Usage: make -C firmware/test        (needs g++ or clang++ with C++17)
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
INCLUDES := -I../lib/MarginProtocol/include -I../lib/MarginChat/include -I../lib/MarginBible/include
SRCS := test_main.cpp test_protocol.cpp test_chat.cpp test_bible.cpp test_companion.cpp \
        ../lib/MarginProtocol/src/crc32.cpp ../lib/MarginProtocol/src/framing.cpp \
        ../lib/MarginProtocol/src/tlv.cpp ../lib/MarginProtocol/src/messages.cpp \
        ../lib/MarginChat/src/chat_model.cpp ../lib/MarginBible/src/bible.cpp
BIN := build/margin_tests

.PHONY: test clean
test: $(BIN)
	./$(BIN)

$(BIN): $(SRCS) $(wildcard *.h) $(wildcard ../lib/*/include/margin/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $@

clean:
	rm -rf build

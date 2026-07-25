CXX ?= g++

# Local VS layout: repo/tests + repo/CommTools
# Remote flat layout: ~/projects/CommTools/tests (sources in parent)
ifeq ($(wildcard ../CommTools/pkg8583.h),)
COMMTOOLS_DIR ?= ..
else
COMMTOOLS_DIR ?= ../CommTools
endif

LIB_DIR ?= $(COMMTOOLS_DIR)/output/Debug
INCLUDES = -I$(COMMTOOLS_DIR) -I.
CXXFLAGS = -Wall -g -std=c++11 -pthread $(INCLUDES)
LDFLAGS = -L$(LIB_DIR) -lCommtools -pthread -Wl,-rpath,$(LIB_DIR)

SRCS = test_main.cpp test_commroute.cpp test_md5.cpp test_des.cpp \
       test_ptrqueue.cpp test_memlog.cpp test_iso8583.cpp test_iniparser.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = test_commtools

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp test_harness.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

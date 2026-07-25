CC=gcc
CXX=g++
AR ?= ar

ifndef PLATFORM 
	PLATFORM = x64
endif 
   
ifndef  DBGF
	DBGF = debug
endif 

OUT_LIB_DIR = ./

ifeq ($(DBGF), debug)
	CFLAGS:=-Wall -g -Ddebug
	CPPFLAGS := -Wall -g -std=c++11 -fPIC -m64 -pthread -Ddebug
	CMODE:=Debug
	OBJDIR=./obj/$(PLATFORM)/Debug
else
	CFLAGS:=-Wall -O3
	CPPFLAGS:=-Wall -O3 -std=c++11 -fPIC -m64 -pthread
	CMODE:=Release
	OBJDIR=./obj/$(PLATFORM)/Release
endif

SRC_FILES_C := $(wildcard *.c)
SRC_OBJ_C := $(patsubst %.c, $(OBJDIR)/%.o, $(notdir $(SRC_FILES_C))) 
SRC_FILES_CPP := $(wildcard *.cpp)
SRC_OBJ_CPP := $(patsubst %.cpp, $(OBJDIR)/%.o, $(notdir $(SRC_FILES_CPP))) 
LIB_DIR := ./output/$(CMODE)
LIB_FILE := libCommtools


all : $(OBJDIR) $(LIB_DIR) $(LIB_DIR)/$(LIB_FILE) 
	@echo "make all completed!"
   
$(OBJDIR):
	@echo "create objs dir "$(OBJDIR)
	mkdir -p $@
   
$(LIB_DIR):
	@echo "create lib dir "$(LIB_DIR)
	mkdir -p $@



$(LIB_DIR)/$(LIB_FILE) : $(SRC_OBJ_CPP) $(SRC_OBJ_C)
	$(AR) -rcs $@.a $^
	$(CXX) -shared -fPIC -o $@.so $^
	cp $@.a  ../lib/$(CMODE)/static
	cp $@.so ../lib/$(CMODE)/share

$(SRC_OBJ_C) : $(OBJDIR)/%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@ 

$(SRC_OBJ_CPP) : $(OBJDIR)/%.o : %.cpp
	$(CXX) -c $(CPPFLAGS) $< -o $@ 

# clean target 
clean: 
ifneq ($(LIB_FILE),) 
	$(RM)  $(SRC_OBJ_CPP) $(SRC_OBJ_C) $(LIB_DIR)/$(LIB_FILE).a  $(LIB_DIR)/$(LIB_FILE).so
endif 
   
.PHONY :all clean    

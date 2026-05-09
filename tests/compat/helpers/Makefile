REPO_ROOT := $(shell git -C "$(dir $(abspath $(lastword $(MAKEFILE_LIST))))" rev-parse --show-toplevel)
C_INCLUDE  := $(REPO_ROOT)/c/include
C_SRC_INC  := $(REPO_ROOT)/c/src
C_BUILD    := $(REPO_ROOT)/c/build

.PHONY: all clean

all: c_dump

c_dump: c_dump.c
	gcc -O2 -o c_dump c_dump.c \
	    -I$(C_INCLUDE) \
	    -I$(C_SRC_INC) \
	    -L$(C_BUILD) \
	    -lncdb \
	    -Wl,-rpath,$(C_BUILD)

clean:
	rm -f c_dump

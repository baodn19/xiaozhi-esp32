# Host build for the fall-detection posture state machine. No ESP-IDF: fall_posture.{h,cc} has
# no target-specific includes by design (see plan/fall_detection_state_machine.md "Architecture"),
# so it compiles here unmodified.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -I../../main/boards/common

COMMON_DIR := ../../main/boards/common
POSTURE_SRC := $(COMMON_DIR)/fall_posture.cc
POSTURE_HDR := $(COMMON_DIR)/fall_posture.h

.PHONY: all test clean

all: fall_replay

fall_replay: main.cc $(POSTURE_SRC) $(POSTURE_HDR)
	$(CXX) $(CXXFLAGS) -o $@ main.cc $(POSTURE_SRC)

fall_posture_test: fall_posture_test.cc $(POSTURE_SRC) $(POSTURE_HDR)
	$(CXX) $(CXXFLAGS) -o $@ fall_posture_test.cc $(POSTURE_SRC)

test: fall_posture_test
	./fall_posture_test

clean:
	rm -f fall_replay fall_posture_test

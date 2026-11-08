# Builds the portable encoder core and its command line tool.
# The Haiku printer add-on is built separately (see docs/).

CXX      ?= g++
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wshadow

CORE_SRC := spl_encoder.cpp spl_algo0x11.cpp spl_models.cpp
CORE_OBJ := $(CORE_SRC:.cpp=.o)

all: pbm2qpdl qpdl2pbm

pbm2qpdl: $(CORE_OBJ) tools/pbm2qpdl.o
	$(CXX) $(LDFLAGS) -o $@ $^

qpdl2pbm: tools/qpdl2pbm.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Test helper; needs the CUPS development files (libcups2-dev, libcupsimage2-dev).
tests/mkraster: tests/mkraster.c
	$(CC) -O2 -o $@ $< -lcupsimage -lcups

%.o: %.cpp spl.h spl_models_generated.inc
	$(CXX) $(CXXFLAGS) -I. -c -o $@ $<

clean:
	rm -f $(CORE_OBJ) tools/*.o pbm2qpdl qpdl2pbm tests/mkraster

.PHONY: all clean

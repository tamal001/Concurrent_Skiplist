GPP = g++ 
FLAGS = -std=c++17 -O3 -g
#FLAGS += -DNDEBUG
LDFLAGS = -pthread

PROGRAMS = main tester

all: $(PROGRAMS)

build:
	mkdir -p $@
	cp -f libjemalloc.so $@/.

main: build
	$(GPP) $(FLAGS) -MMD -MP -MF build/$@.d -o $@ $@.cpp $(LDFLAGS)

-include $(addprefix build/,$(addsuffix .d, $(PROGRAMS)))

tester: build
	$(GPP) $(FLAGS) -MMD -MP -MF build/$@.d -o $@ $@.cpp $(LDFLAGS)

-include $(addprefix build/,$(addsuffix .d, $(PROGRAMS)))

clean:
	rm -rf $(PROGRAMS) build
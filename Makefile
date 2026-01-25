CXX ?= g++
#FLAGS := -std=c++23 -g -Wall -O0 -ldl 
FLAGS := -std=c++23 -g -Wall -O3 -march=native -ldl 


all: jit_compiler 

jit_compiler: jit_compiler.cpp operators.hpp
	$(CXX) $(FLAGS) -o jit_compiler jit_compiler.cpp

clean:
	rm -f jit_compiler query gen.cpp *.so *.gch q1.cpp q_join.cpp

format:
	clang-format -i *.hpp *.cpp data-generator/*.hpp data-generator/*.cpp

.PHONY: all clean format

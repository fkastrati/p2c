CXX ?= g++
FLAGS := -std=c++23 -g -Wall -O0 # on old compilers: -lfmt

# (1) run p2c to generate query code, format the generated code if clang-format exists
# (2) compile generated code


# compile the query compiler p2c
p2c: p2c.cpp operators.hpp
	$(CXX) $(FLAGS) -o p2c p2c.cpp

clean:
	rm -f p2c query gen.cpp

format:
	clang-format -i *.hpp *.cpp data-generator/*.hpp data-generator/*.cpp

.PHONY: clean format

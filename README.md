# Plan-to-C Query Compiler for MacOS

This repository is a fork of  
https://github.com/viktorleis/p2c

with number of changes done to the original code.

## Tested Environment
```
Apple clang version 17.0.0 (clang-1700.6.3.2)
Target: arm64-apple-darwin24.6.0
Thread model: posix
```
## Changes from Upstream
- Fixed build issues with Apple Clang
- Portability fixes
- Large code restructuring and refactoring in order to allow for better readability and code extension
- Added Print operator: The query logic is now entirely contained within the operator tree.
- Added Limit operator: introduces the ability to stop execution early. By using goto label, we successfully break out of the deeply nested for loops that HashJoin and Scan generate, which a simple break would not achieve.

## P2C: Plan-to-C Query Compiler

p2c is an educational compiling query engine.
Given an operator tree (query plan), it generates C++ code (hence plan-to-code).
The generated code is nicely formatted and can be inspected in `gen.cpp`.

Components:
- **`p2c.cpp`** - Main query compiler that generates C++ code from operator trees
- **`operator.hpp`** - Query operators and expression types
- **`types.hpp`** - Type system supporting integers, doubles, strings, dates
- **`tpch.hpp`** - TPC-H schema definitions and database autoloading
- **`io.hpp`** - Memory-mapped I/O with columnar data access
- **`queryFrame.cpp`** - Runtime framework that executes generated code
- **`operators.hpp`** - Physical operators and expressions

## Getting Started

You will need:
- A C++23 compiler (gcc >= 14, clang >= 19)
- Alternatively: A C++20 compiler *and* [`libfmt`](https://github.com/fmtlib/fmt)
- Optionally: clang-format to format generated code

To run a query, follow these steps:
1. **Data Generation**: Convert TPC-H CSV data to optimized binary columnar format
2. **Code Generation**: Transform query operators into optimized C++ code
3. **Compilation**: Build executable with generated code and runtime framework
4. **Execution**: Load data using memory-mapped files and execute generated code

### Data Generation:
```bash
cd data-generator
./generate-data.sh
```

This creates scale factor 1 TPC-H data in `data-generator/output/`.
The script first uses the `dbgen` tool to generate csv files, then reads and converts them to binary data.

### Code Generation & Compilation:
```bash
make p2c   # Build the query compiler and sample query in p2c.cpp#main
make query # Compile generated query code
make       # Does all of the above 
```

### Execution:
```bash
# Run with default data location
./query

# Specify data path and run count
./query data-generator/output 3
```

The current implementation includes a sample query equivalent to TPC-H query 5.

.PHONY: all test clean

all:
	cmake -S . -B build
	cmake --build build

test: all
	ctest --test-dir build --output-on-failure

clean:
	cmake -E remove_directory build


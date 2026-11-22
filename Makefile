CXX := clang++
WASM_CXX := clang++

CXXFLAGS := -std=c++20 -O2 -Wall -Wextra
WASM_FLAGS := --target=wasm32 -std=c++20 -O3 -nostdlib -fno-exceptions -fno-rtti \
              -mbulk-memory -Wl,--no-entry -Wl,--export-dynamic -Wl,--strip-all

.PHONY: all clean serve

all: bin/ssg static/sim.wasm
	./bin/ssg

bin/ssg: src/build.cpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) src/build.cpp -o bin/ssg

static/sim.wasm: src/sim.cpp
	$(WASM_CXX) $(WASM_FLAGS) src/sim.cpp -o static/sim.wasm

serve: all
	python3 -m http.server 8000 --directory docs

clean:
	rm -rf bin docs .docs-tmp static/sim.wasm

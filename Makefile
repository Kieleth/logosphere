.PHONY: all build test eden clean help generate-ontology

all: build

# File-level deps: regeneration only runs when YAML or the generator
# scripts change. Combined with write-if-changed in the Python scripts,
# `make eden` on an unchanged tree is a no-op for cmake instead of a
# full library rebuild.
GENERATED_SOURCES := \
	src/generated/logosphere_ontology.h \
	src/generated/logosphere_ontology_registry.cpp \
	src/generated/logosphere_ontology_registry.h

ONTOLOGY_INPUTS := \
	schema/logosphere.yaml \
	schema/malleus.yaml \
	scripts/generate_ontology.py \
	scripts/generate_registry.py

$(GENERATED_SOURCES): $(ONTOLOGY_INPUTS)
	@cd scripts && python generate_ontology.py

generate-ontology: $(GENERATED_SOURCES)

build: $(GENERATED_SOURCES)
	@cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64" -DCMAKE_BUILD_TYPE=Release
	@cmake --build build

test: build
	@echo "Running tests..."
	@./build/logosphere-tests --test

eden: build
	@cp -f build/default.metallib build/eden/default.metallib 2>/dev/null || true
	@echo "Running Eden..."
	@./build/eden/eden

clean:
	@rm -rf build src/generated

help:
	@echo "Logosphere Build System"
	@echo "======================"
	@echo ""
	@echo "  make                   Build everything (generates ontology first)"
	@echo "  make generate-ontology Generate C++ headers from ontology YAML"
	@echo "  make test              Run test suite"
	@echo "  make eden              Build and run Eden example"
	@echo "  make clean             Remove build directory and generated files"

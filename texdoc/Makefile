PNGS = AuxiliaryInterfaces.png DListVocabulary.png Pool.png bitstrings.png \
	Basic.png Dictionary.png StreamBuffer.png list.png BloomFilter.png \
	Iterator.png ValArray.png Circular.png ListVocabulary.png \
	VectorVocabulary.png Containers.png Memorymanagement.png Vocabulary.png

ROOT := ..
BUILD_DIR ?= $(ROOT)/build
CCL_LIBRARY ?= $(BUILD_DIR)/lib/libccl.a
DOC_DIR ?= $(ROOT)/doc
DOC_PDF ?= $(DOC_DIR)/ccl.pdf
TEX_SOURCES := $(filter-out table.tex,$(wildcard *.tex))

CC ?= cc
CMAKE ?= cmake
PDFLATEX ?= pdflatex
MAKEINDEX ?= makeindex

.PHONY: all clean html zip

all: $(DOC_PDF)

$(DOC_PDF): ccl.pdf
	mkdir -p $(DOC_DIR)
	cp -f $< $@

ccl.pdf: $(TEX_SOURCES) table.tex $(PNGS) $(ROOT)/include/containers.h \
	$(ROOT)/include/range.h
	$(PDFLATEX) -interaction=nonstopmode -halt-on-error ccl.tex
	$(MAKEINDEX) ccl.idx
	$(PDFLATEX) -interaction=nonstopmode -halt-on-error ccl.tex
	$(PDFLATEX) -interaction=nonstopmode -halt-on-error ccl.tex
	$(PDFLATEX) -interaction=nonstopmode -halt-on-error ccl.tex
	mkdir -p $(DOC_DIR)
	cp -f $@ $(DOC_PDF)

table.tex: dotable $(ROOT)/include/containers.h \
	$(ROOT)/include/valarraygen.h $(ROOT)/include/stringlistgen.h
	./dotable

$(CCL_LIBRARY):
	$(CMAKE) -S $(ROOT) -B $(BUILD_DIR) -DBUILD_TESTING=OFF
	$(CMAKE) --build $(BUILD_DIR) --target ccl

HAS_ASAN := $(shell nm $(CCL_LIBRARY) 2>/dev/null | grep -q __asan && echo yes)
ifeq ($(HAS_ASAN),yes)
  CFLAGS += -fsanitize=address,undefined
endif
HAS_GCOV := $(shell nm $(CCL_LIBRARY) 2>/dev/null | grep -q __gcov && echo yes)
ifeq ($(HAS_GCOV),yes)
  CFLAGS += --coverage
endif

dotable: dotable.c $(CCL_LIBRARY)
	$(CC) $(CFLAGS) -g -o $@ -I$(ROOT)/include dotable.c $(CCL_LIBRARY)

html: ccl.html

ccl.html: tth ccl.tex $(PNGS) table.tex
	./tth ccl.tex

tth: tth.c
	$(CC) -o $@ -g tth.c

clean:
	rm -f table.tex ccl.pdf ccl.html dotable tth ccldoc.zip \
		ccl.aux ccl.idx ccl.ilg ccl.ind ccl.log ccl.out ccl.toc \
		*.gcda *.gcno

zip: ccl.pdf
	zip -9 ccldoc.zip dotable.c ccl.tex Range.tex tth.c ccl.pdf \
		$(PNGS) Makefile

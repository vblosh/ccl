PNGS = AuxiliaryInterfaces.png DListVocabulary.png Pool.png bitstrings.png \
	Basic.png Dictionary.png StreamBuffer.png list.png BloomFilter.png \
	Iterator.png ValArray.png Circular.png ListVocabulary.png \
	VectorVocabulary.png Containers.png Memorymanagement.png Vocabulary.png

ROOT := ..
BUILD_DIR ?= $(ROOT)/build
CCL_LIBRARY ?= $(BUILD_DIR)/lib/libccl.a
TEX_SOURCES := $(filter-out table.tex,$(wildcard *.tex))

CC ?= cc
CMAKE ?= cmake
PDFLATEX ?= pdflatex
MAKEINDEX ?= makeindex

.PHONY: all clean html zip

all: ccl.pdf

ccl.pdf: $(TEX_SOURCES) table.tex $(PNGS) $(ROOT)/include/containers.h \
	$(ROOT)/include/range.h
	$(PDFLATEX) -interaction=nonstopmode -halt-on-error ccl.tex
	$(MAKEINDEX) ccl.idx
	$(PDFLATEX) -interaction=nonstopmode -halt-on-error ccl.tex
	$(PDFLATEX) -interaction=nonstopmode -halt-on-error ccl.tex
	$(PDFLATEX) -interaction=nonstopmode -halt-on-error ccl.tex

table.tex: dotable $(ROOT)/include/containers.h \
	$(ROOT)/include/valarraygen.h $(ROOT)/include/stringlistgen.h
	./dotable

$(CCL_LIBRARY):
	$(CMAKE) -S $(ROOT) -B $(BUILD_DIR) -DBUILD_TESTING=OFF
	$(CMAKE) --build $(BUILD_DIR) --target ccl

dotable: dotable.c $(CCL_LIBRARY)
	$(CC) -g -o $@ -I$(ROOT)/include dotable.c $(CCL_LIBRARY)

html: ccl.html

ccl.html: tth ccl.tex $(PNGS) table.tex
	./tth ccl.tex

tth: tth.c
	$(CC) -o $@ -g tth.c

clean:
	rm -f table.tex ccl.pdf ccl.html dotable tth ccldoc.zip \
		ccl.aux ccl.idx ccl.ilg ccl.ind ccl.log ccl.out ccl.toc

zip: ccl.pdf
	zip -9 ccldoc.zip dotable.c ccl.tex Range.tex tth.c ccl.pdf \
		$(PNGS) Makefile

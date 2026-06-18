mkimage: mkimage.cpp
	g++ -Wall -o $@ $< mincrypt/sha256.c -Imincrypt/include

.PHONY: install
install: mkimage
	mkdir -p $(out)/bin/
	cp -v mkimage $(out)/bin/mkimage

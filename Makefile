C=amber.c
F=amber.1 Makefile test.sh

P=/usr/local
M=$P/man/man1

V=0.1

CFLAGS=-g

amber: amber.c Makefile
	cc $(CFLAGS) -DVER='"$V"' amber.c -o amber

install-bin: amber
	install -S amber $P/bin

install-man:
	cp amber.1 $M

install: install-bin install-man

tar: $C $F
	rm -rf amber-$V amber-$V.tar.gz
	mkdir amber-$V
	cp -p $C $F amber-$V
	tar cvf amber-$V.tar amber-$V
	gzip amber-$V.tar

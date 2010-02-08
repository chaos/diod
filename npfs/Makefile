CFLAGS=-Wall -Werror -g
INSTALLPREFIX:=/usr/local
export INSTALLPREFIX

all:
	make -C libnpfs
	#make -C libnpclient
	#make -C fs

clean:
	make -C libnpfs clean
	#make -C libnpclient clean
	#make -C fs clean
	#rm -f *~ include/*~

install:
	#make -C libnpfs install
	#make -C libnpclient install

distclean:

all:
	gcc -Wall -g segmenter.c -o segmenter -lavformat -lavcodec -lavutil -lavfilter -lavdevice -lswresample -lswscale -lm -lz -lpthread

m3u8:
	gcc -Wall -g m3u8.c -o m3u8 -lavformat -lavcodec -lavutil -lavfilter -lavdevice -lswresample -lswscale -lm -lz -lpthread

clean:
	rm segmenter

install: segmenter
	cp segmenter /usr/local/bin/

uninstall:
	rm /usr/local/bin/segmenter

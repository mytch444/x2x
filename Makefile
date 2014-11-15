#
# Copyright (c) 2014 Mytchel Hammond <mytchel at openmailbox dot org>
# 

all: x2x

x2x: x2x.c
	gcc x2x.c -o x2x -lXtst -lXext -lX11

clean:
	rm x2x

install:
	install -Dm 755 x2x /usr/local/bin/x2x

uninstall:
	rm /usr/local/bin/x2x

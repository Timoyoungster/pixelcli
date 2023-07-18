main: pixelcli.c
	gcc pixelcli.c -o pixelcli -lpng
	./pixelcli

debug:
	gcc -g pixelcli.c -o pixelcli_debug -lpng
	gdb pixelcli_debug

recvmake: file_receiver7.c
	gcc file_receiver7.c lodepng.c vmac.a libz.a -pthread -Wall -lm
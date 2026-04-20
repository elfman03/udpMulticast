OPTS=/MD /EHsc /Zi
ELIBS=ws2_32.lib mswsock.lib advapi32.lib

#
# Use existance of PROGRAMFILES environment variable to determine windows
#
ifdef PROGRAMFILES
WINDOWS=1
udpMulticast.exe: udpMulticast.c
	cl udpMulticast.c /link $(ELIBS)

clean:
	cmd /c del /q udpMulticast.exe udpMulticast.obj

test:
	./udpMulticast.exe -serve 239.255.42.42 5004 -serveon 7777 192.168.1.1 192.168.1.2

test_record: 
	./udpMulticast.exe -record 239.255.42.42 5004 capture.bin

test_play:
	./udpMulticast.exe -playUnicast capture.bin -serveon 7777 192.168.1.1 192.168.1.2

test_play2:
	./udpMulticast.exe -playMulticast 239.255.42.42 5004 capture.bin

test_play3:
	./udpMulticast.exe -playMulticast 239.255.42.42 5004 capture.bin -interface 192.168.1.108

else

WINDOWS=0

udpMulticast: udpMulticast.c
	gcc -o udpMulticast udpMulticast.c 

clean:
	rm udpMulticast

test:
	./udpMulticast -serve 239.255.42.42 5004 -serveon 7777 192.168.1.1 192.168.1.2

test_record: 
	./udpMulticast -record 239.255.42.42 5004 capture.bin

test_play:
	./udpMulticast -playUnicast capture.bin -serveon 7777 192.168.20.10 192.168.20.11 192.168.20.125

test_play2:
	./udpMulticast -playMulticast 239.255.42.42 5004 capture.bin

test_play3:
	./udpMulticast -playMulticast 239.255.42.42 5004 capture.bin -interface 192.168.1.108
endif



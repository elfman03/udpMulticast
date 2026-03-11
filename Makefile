OPTS=/MD /EHsc /Zi
ELIBS=ws2_32.lib mswsock.lib advapi32.lib

udpMulticast.exe: udpMulticast.c
	cl udpMulticast.c /link $(ELIBS)

clean:
	cmd /c del /q udpMulticast.exe udpMulticast.obj

test:
	./udpMulticast.exe -serve 239.255.42.42 5004 -serveon 7777 192.168.1.1 192.168.1.2

test_record: 
	./udpMulticast.exe -record capture.bin 239.255.42.42 5004

test_play:
	./udpMulticast.exe -playUnicast capture.bin -serveon 7777 192.168.1.1 192.168.1.2

test_play2:
	./udpMulticast.exe -playMulticast capture.bin 239.255.42.42 5004

test_play3:
	./udpMulticast.exe -playMulticast capture.bin 239.255.42.42 5004 -interface 192.168.1.108


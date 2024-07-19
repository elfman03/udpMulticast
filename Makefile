OPTS=/MD /EHsc /Zi
ELIBS=ws2_32.lib mswsock.lib advapi32.lib

udpMulticast.exe: udpMulticast.c
	cl udpMulticast.c /link $(ELIBS)

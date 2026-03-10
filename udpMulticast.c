/*
 * Simple listener.c program for UDP multicast
 *
 * Adapted from:
 * https://gist.github.com/hostilefork/f7cae3dc33e7416f2dd25a402857b6c6
 * http://ntrg.cs.tcd.ie/undergrad/4ba2/multicast/antony/example.html
 *
 * Changes:
 *   - Compiles for Windows as well as Linux
 *   - recorder with timing
 *   - Record and playback in same app
 */

#ifdef _WIN32
    #include <Winsock2.h> // before Windows.h, else Winsock 1 conflict
    #include <Ws2tcpip.h> // needed for ip_mreq definition for multicast
    #include <Windows.h>
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <time.h>
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

unsigned int getTicks() {
  unsigned int ret;

#ifdef _WIN32
  ret=(unsigned int)GetTickCount();
#else
  TODO.  PUT IN SOME LINUX MILLISECOND COLLECTION HERE
#endif

  return ret;
}

#define PACKET_MAX (65536-8)
#define MODE_UNK    0
#define MODE_RECORD 1
#define MODE_PLAY   2
#define MODE_SERVE  3
//
//  Globals are not great but for a simple app they are so easy to use.
//

int theMode=MODE_UNK;       // record or play?
const char *theFileName=0;  // the input/output filename
FILE *theFile=0;            // the input/output FILE
const char *theGroup=0;     // the multicast group
int thePortIn=0;            // the multicast port
int thePortOut=0;           // the outgoing service port
const char *theInterfaceOut=0; // The output interface to use (null for INADDR_ANY or the ip address of adapter)

const char *modeStrings[4]={"Unknown","Record","Play","Serve"};

typedef struct packetType {
  unsigned int tick;
  unsigned int payloadSz;
  char payload[PACKET_MAX];
} packetType;

void Usage(const char *msg) {
  if(msg) {
    fprintf(stderr,"ERROR: %s\n\n",msg);
  }
  fprintf(stderr,"udpMulticast - a UDP multicast recorder/replayer\n\nUsage:\n\n");
  fprintf(stderr,"  udpMulticast -record {filename} {group} {port}\n");
  fprintf(stderr,"  udpMulticast -play   {filename} {group} {port} [-interface {ip}]\n\n\n");
  fprintf(stderr,"Examples:\n");
  fprintf(stderr,"  udpMulticast -record capture.bin 239.255.42.42 5004)\n");
  fprintf(stderr,"     - capture udp broadcast stream from selected JTech hdbitt extenders)\n\n");
  fprintf(stderr,"  udpMulticast -play   capture.bin 239.255.42.42 5004)\n");
  fprintf(stderr,"     - replay udp stream previously captured from JTECH hdbitt extender)\n\n");
  fprintf(stderr,"  udpMulticast -play   capture.bin 239.255.42.42 5004 -interface 192.168.1.1)\n");
  fprintf(stderr,"     - replay stream as above but to the interface with IP address 192.168.1.1)\n\n");
  fprintf(stderr,"  udpMulticast -serve   239.255.42.42 5004 -serveon 192.168.1.1 7777)\n");
  fprintf(stderr,"     - retransmit incoming stream as clients who connect tcp 192.168.1.1:7777)\n\n");
}

int handleArgs(int argc, char *argv[]) {
  char msg[1024];

  // returns -1 on error, 0 on success
  //
  if (argc != 5 && argc!=7) {
    Usage("Invalid parameter count.  Four or six required");
    return -1;
  }
  if (!strcmp(argv[1],"-record")) {
    theMode=MODE_RECORD;
  }
  if (!strcmp(argv[1],"-play")) {
    theMode=MODE_PLAY;
  }
  if (!strcmp(argv[1],"-serve")) {
    theMode=MODE_SERVE;
  }
  if(theMode==MODE_UNK) {
    Usage("Invalid first parameter.  must be '-record', '-play', or '-serve'");
    return -1;
  }

  // Handle hardcoding interface
  //
  if(theMode==MODE_SERVE) {
    if(argc!=7) {
      Usage("Serve mode requires 6 arguments");
      return -1;
    }
    if(strcmp(argv[4],"-serveon")) {
      Usage("Invalid 3rd parameter.  must be '-serveon'");
      return -1;
    }
    theGroup=argv[2]; // e.g., 239.255.255.250 for SSDP
    thePortIn=atoi(argv[3]); // 0 if error, which is an invalid port
    theInterfaceOut=argv[5];
    thePortOut=atoi(argv[6]); // 0 if error, which is an invalid port
    if(!thePortOut) {
      sprintf(msg,"Invalid inbound port number '%s'",argv[4]);
      Usage(msg);
      return -1;
    }
  } 
  if((theMode==MODE_RECORD || theMode==MODE_UNK) && argc>5) {
    Usage("Mode does not support this parameter count");
    return -1;
  }
  if(theMode==MODE_PLAY) {
    if(argc==7 && strcmp(argv[5],"-interface")) {
      Usage("Invalid 5th parameter.  must be '-interface'");
      return -1;
    }
    if(argc==7) {
      theInterfaceOut=argv[6];
    }
  }

  if(theMode!=MODE_SERVE) {
    theFileName=argv[2];  // e.g., record.bin
    if(theMode==MODE_RECORD) {
      theFile=fopen(theFileName,"wb");
    } else {
      theFile=fopen(theFileName,"rb");
    }
    if(!theFile) {
      sprintf(msg,"Cannot open file '%s'",theFileName);
      Usage(msg);
      return 1;
    }
    theGroup=argv[3]; // e.g., 239.255.255.250 for SSDP
    thePortIn=atoi(argv[4]); // 0 if error, which is an invalid port
  }

  if(!thePortIn) {
    sprintf(msg,"Invalid inbound port number '%s'",argv[4]);
    Usage(msg);
    return -1;
  }
  if(theMode!=MODE_SERVE) {
    fprintf(stderr,"Lets Go!  %s %s udp://%s:%d interface %s\n",modeStrings[theMode],theFileName,theGroup,thePortIn,theInterfaceOut);
  } else {
    fprintf(stderr,"Lets Go!  %s udp://%s:%d -- serve on tcp://%s:%d\n",modeStrings[theMode],theGroup,thePortIn,theInterfaceOut,thePortOut);
  }
  return 0;
}

int recorder(int fd) {
    unsigned int firstTick=0;
    packetType packet;
    int packetCt=0;
    int netBytes=0;

  // allow multiple sockets to use the same PORT number
  //
  u_int yes = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*) &yes, sizeof(yes)) < 0) {
     perror("Reusing ADDR failed");
     return 1;
  }

  // set up destination address
  //
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // differs from sender
  addr.sin_port = htons(thePortIn);

  // bind to receive address
  //
  if (bind(fd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
      perror("bind");
      return 1;
  }

  // use setsockopt() to request that the kernel join a multicast group
  //
  struct ip_mreq mreq;
  mreq.imr_multiaddr.s_addr = inet_addr(theGroup);
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*) &mreq, sizeof(mreq)) < 0) {
      perror("setsockopt");
      return 1;
  }

  // now just enter a read-print loop
  //
  while (1) {
      int addrlen = sizeof(addr);
      int nbytes = recvfrom(
          fd,
          packet.payload,
          PACKET_MAX,
          0,
          (struct sockaddr *) &addr,
          &addrlen
      );
      if (nbytes < 0) {
          perror("recvfrom");
          return 1;
      }
      packet.tick=getTicks();
      if(!firstTick) { firstTick=packet.tick; }
      packet.tick=packet.tick-firstTick;
      packet.payloadSz=(unsigned int)nbytes;
      if(theFile) {
        fwrite(&packet,1,nbytes+8,theFile);
      } else {
        fprintf(stderr,"Funnel payload ... bytes=%d\n",nbytes);
      }
      packetCt++;
      netBytes=netBytes+nbytes;
      if(packetCt%500==1) {
        fprintf(stderr,"t=%u ct=%d netb=%d b=%d\n",packet.tick,packetCt,netBytes,packet.payloadSz);
      }
  }
  return 0;
}

int player(int fd) {
  unsigned int tick,firstTick=0;
  packetType packet;
  int packetCt=0;
  int dataPacketCt=0;
  int zeroPacketCt=0;
  int netBytes=0;

  // set up destination address
  //
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(theGroup);
  addr.sin_port = htons(thePortIn);
  
  // 
  // Specific interface for multicast https://stackoverflow.com/questions/9701561/how-to-specify-the-multicast-send-interface-in-python
  //
  if(theInterfaceOut) {
    unsigned long sa=inet_addr(theInterfaceOut);
    int tmp=setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, (char*) &sa, sizeof(sa));
    if(setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, (char*) &sa, sizeof(sa)) < 0) {
      fprintf(stderr,"Error initializing custom interface : WinSock Error: %d\n",WSAGetLastError());
      return 1;
    }
  }

  // get first header
  //
  fread(&packet,1,8,theFile);
  //
  while (!feof(theFile)) {
    //
    // reconsider this if we want to send the zero length packets too.  for now skip them.
    //
    if(packet.payloadSz) {
      // fetch remainder of packet
      //
      fread(packet.payload,1,packet.payloadSz,theFile);

      // Is this packet supposed to be in the future?  Is so, wait for its timeslot.
      //
      tick=getTicks();
      if(!firstTick) { firstTick=tick; }
      unsigned int toffset=tick-firstTick;
      if(packet.tick > toffset) {
#ifdef _WIN32
        Sleep(packet.tick - toffset);
#else
        TODO.  PUT IN SOME LINUX MILLISECOND USLEEP HERE
#endif
      }

      // It is time.  Lets send it
      //
      int nbytes = sendto(
         fd,
         packet.payload,
         packet.payloadSz,
         0,
         (struct sockaddr*) &addr,
         sizeof(addr)
      );
      if (nbytes < 0) {
         perror("sendto");
         return 1;
      }
      dataPacketCt++;
      netBytes=netBytes+packet.payloadSz;
    } else {
      zeroPacketCt++;
    }
    packetCt++;
    if(packetCt%500==1) {
      fprintf(stderr,"t=%u dct=%d zct=%d netb=%dd\n",packet.tick,dataPacketCt,zeroPacketCt,netBytes);
    }

    // read next header
    fread(&packet,1,8,theFile);
  }
  fprintf(stderr,"EOF.  Wrote %d bytes from %d data packets. skipped %d zero packets\n",netBytes,packetCt,zeroPacketCt);
  return 0;
}

int server(int fd) {
}

int main(int argc, char *argv[]) {
    char msg[1024];
    int ret=handleArgs(argc,argv);
    if(ret) { return ret; }

#ifdef _WIN32
    //
    // Initialize Windows Socket API with given VERSION.
    //
    WSADATA wsaData;
    if (WSAStartup(0x0101, &wsaData)) {
        perror("WSAStartup");
        return 1;
    }
#endif

    // create what looks like an ordinary UDP socket
    //
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    if(theMode==MODE_RECORD) { recorder(fd); }
    if(theMode==MODE_PLAY)   { player(fd);   }
    if(theMode==MODE_SERVE)  { server(fd);   }

    if(theFile) { fclose(theFile); }
#ifdef _WIN32
    //
    // Program never actually gets here due to infinite loop that has to be
    // canceled, but since people on the internet wind up using examples
    // they find at random in their own code it's good to show what shutting
    // down cleanly would look like.
    //
    WSACleanup();
#endif

    return 0;
}

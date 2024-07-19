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
#endif

  return ret;
}

#define PACKET_MAX (65536-8)
#define MODE_UNK    0
#define MODE_RECORD 1
#define MODE_PLAY   2
//
//  Globals are not great but for a simple app they are so easy to use.
//

int theMode=MODE_UNK;     // record or play?
const char *theFile=0;    // the input/output file
const char *theGroup=0;   // the multicast group
int thePort=0;            // the multicast port

const char *modeStrings[3]={"Unknown","Record","Play"};

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
  fprintf(stderr,"  udpMulticast -play   {filename} {group} {port}\n\n\n");
  fprintf(stderr,"Examples:\n");
  fprintf(stderr,"  udpMulticast -record capture.bin 239.255.42.42 5004)\n");
  fprintf(stderr,"     - capture udp broadcast stream from selected JTech hdbitt extenders)\n\n");
}

int handleArgs(int argc, char *argv[]) {
  // returns -1 on error, 0 on success
  //
  if (argc != 5) {
    Usage("Invalid parameter count.  Four required");
    return -1;
  }
  if (!strcmp(argv[1],"-record")) {
    theMode=MODE_RECORD;
  }
  if (!strcmp(argv[1],"-play")) {
    theMode=MODE_PLAY;
  }
  if(theMode==MODE_UNK) {
    Usage("Invalid first parameter.  must be '-record' or '-play'");
    return -1;
  }
  theFile=argv[2];  // e.g., record.bin
  theGroup=argv[3]; // e.g., 239.255.255.250 for SSDP
  thePort=atoi(argv[4]); // 0 if error, which is an invalid port
  if(!thePort) {
    char msg[1024];
    sprintf(msg,"Invalid port number '%s'",argv[4]);
    Usage(msg);
    return -1;
  }
  fprintf(stderr,"Lets Go!  %s %s udp://%s:%d\n",modeStrings[theMode],theFile,theGroup,thePort);
  return 0;
}

int main(int argc, char *argv[])
{
    unsigned int firstTick=0;
    packetType packet;
    FILE *filer=0;

    int ret=handleArgs(argc,argv);
    if(ret) { return ret; }

    if(theMode==MODE_RECORD) {
      filer=fopen(theFile,"wb");
    } else {
      filer=fopen(theFile,"rb");
    }
    if(!filer) {
      sprintf(packet.payload,"Cannot open file '%s'",theFile);
      Usage(packet.payload);
      return 1;
    }


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
    addr.sin_port = htons(thePort);

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
    if (
        setsockopt(
            fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*) &mreq, sizeof(mreq)
        ) < 0
    ){
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
        fwrite(&packet,1,nbytes+8,filer);
        fprintf(stderr,"t=%u b=%d",packet.tick,packet.payloadSz);
     }

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

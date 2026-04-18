/*
 * Multicast [video] UDP multicast stream receiver, transmitter, and unicast adapter
 *
 * Adapted from:
 * https://gist.github.com/hostilefork/f7cae3dc33e7416f2dd25a402857b6c6
 * http://ntrg.cs.tcd.ie/undergrad/4ba2/multicast/antony/example.html
 *
 * Changes:
 *   - Compiles for Windows as well as Linux
 *   - recorder with timing (to facilitate timed playback)
 *   - Record and playback in same app
 *   - Record and playback in same app
 *   - Receive multicast and retransmit as unicast
 *   - Playback recorded file as multicast transmission
 *   - Playback recorded file as unicast
 */

#ifdef _WIN32
    #include <Winsock2.h> // before Windows.h, else Winsock 1 conflict
    #include <Ws2tcpip.h> // needed for ip_mreq definition for multicast
    #include <Windows.h>
#else
    #include <unistd.h>
    #include <errno.h>
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
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC,&ts);  // TODO error handling?
  ret=(unsigned int) (ts.tv_sec*1000)+(ts.tv_nsec/1000);
#endif

  return ret;
}

#define PACKET_MAX (65536-8)
#define MODE_UNK     0
#define MODE_RECORD  1
#define MODE_PLAY_MC 2
#define MODE_PLAY_UC 3
#define MODE_SERVE   4
//
//  Globals are not great but for a simple app they are so easy to use.
//

FILE *logfile=0;
int theMode=MODE_UNK;       // record or play or serve?
const char *theFileName=0;  // the input/output filename
FILE *theFile=0;            // the input/output FILE
const char *theGroup=0;     // the multicast group
int thePortIn=0;            // the multicast port
int thePortOut=0;           // the outgoing service port
const char *theInterfaceOut=0; // PLAY MODE - The output interface to use (null for INADDR_ANY or the ip address of adapter)
const char *theDestIP[8]={0,0,0,0,0,0,0,0}; // SERVE MODE - The destination IP addresses
int theDestSock[8]={0,0,0,0,0,0,0,0};       // SERVE MODE - The destination connected socket

const char *modeStrings[5]={"Unknown","Record","PlayMulticast","PlayUnicast","Serve"};

typedef struct packetType {
  unsigned int tick;
  unsigned int payloadSz;
  char payload[PACKET_MAX];
} packetType;

void Usage(const char *msg) {
  if(msg) {
    fprintf(logfile,"ERROR: %s\n\n",msg);
  }
  fprintf(logfile,"udpMulticast - a UDP multicast recorder/replayer and unicast converter\n\nUsage:\n\n");
  fprintf(logfile,"  udpMulticast -serve  {group} {port} -serveon {port} {ip} [{ip}] [{ip}] [{ip}] [{ip}] [{ip}] [{ip}] [{ip}]\n");
  fprintf(logfile,"  udpMulticast -record {group} {port} {filename}\n");
  fprintf(logfile,"  udpMulticast -playMulticast {group} {port} {filename} [-interface {ip}]\n");
  fprintf(logfile,"  udpMulticast -playUnicast   {filename} -serveon {port} {ip} [{ip}] [{ip}] [{ip}] [{ip}] [{ip}] [{ip}] [{ip}]\n\n");
  fprintf(logfile,"Examples:\n");
  fprintf(logfile,"  udpMulticast -serve   239.255.42.42 5004 -serveon 7777 192.168.100.10 192.168.1000.11\n");
  fprintf(logfile,"     - unicast retransmit incoming multicast stream to port 7777 at client ips [up to 8])\n\n");
  fprintf(logfile,"  udpMulticast -record 239.255.42.42 5004 capture.bin\n");
  fprintf(logfile,"     - capture udp multicast broadcast stream from selected JTech hdbitt extenders\n\n");
  fprintf(logfile,"  udpMulticast -playMulticast 239.255.42.42 5004 capture.bin\n");
  fprintf(logfile,"     - replay udp stream previously captured from JTECH hdbitt extender via multicast\n\n");
  fprintf(logfile,"  udpMulticast -playMulticast 239.255.42.42 5004 capture.bin -interface 192.168.1.1\n");
  fprintf(logfile,"     - replay stream as above but to the interface with IP address 192.168.1.1\n\n");
  fprintf(logfile,"  udpMulticast -playUnicast capture.bin -serveon 7777 192.168.100.10 192.168.1000.11\n");
  fprintf(logfile,"     - unicast transmit recorded capture.bin to port 7777 at client ips [up to 8]\n\n");
}

//
// The longest method.  It populates what we want to do based on command line
// returns -1 on error, 0 on success
//
int handleArgs(int argc, char *argv[]) {
  char msg[1024];

  // Sanity check arg count for each mode and populate mode. 
  //
  if (argc < 5)    { Usage("Invalid parameter count.  At least four required");     return -1; }
  if (!strcmp(argv[1],"-record")) {
    theMode=MODE_RECORD;
    if (argc != 5) { Usage("record mode: Invalid parameter count.  Four required"); return -1; }
  }
  if (!strcmp(argv[1],"-playMulticast")) {
    theMode=MODE_PLAY_MC;
    if (argc != 5 && argc!=7)                    { Usage("play multicast mode: Invalid param count.  need 4 or 6");   return -1; }
    if (argc==7 && strcmp(argv[5],"-interface")) { Usage("Invalid 5th parameter.  must be '-interface'");             return -1; }
  }
  if (!strcmp(argv[1],"-playUnicast")) {
    theMode=MODE_PLAY_UC;
    if (argc <6 || argc>13)         { Usage("play unicast mode:  Invalid parameter count.  five to twelve required"); return -1; }
    if (strcmp(argv[3],"-serveon")) { Usage("Invalid 2nd parameter.  must be '-serveon'");                            return -1; }
  }
  if (!strcmp(argv[1],"-serve")) {
    theMode=MODE_SERVE;
    if (argc <7 || argc>14)         { Usage("serve mode:  Invalid parameter count.  six to thirteen required");  return -1; }
    if (strcmp(argv[4],"-serveon")) { Usage("Invalid 3rd parameter.  must be '-serveon'");                       return -1; }
  }
  if(theMode==MODE_UNK) { Usage("Invalid first parameter.  must be '-record', '-playMulticast', '-playUnicast', or '-serve'"); return -1; }

  // For modes that require a file open and verify file handle
  //
  if(theMode==MODE_RECORD || theMode==MODE_PLAY_MC || theMode==MODE_PLAY_UC) {
    if(theMode==MODE_PLAY_UC) {
      theFileName=argv[2];   // e.g., record.bin
    } else {
      theFileName=argv[4];   // e.g., record.bin
    }
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
  }

  // For modes that require a multicast group parse it
  //
  if(theMode==MODE_RECORD || theMode==MODE_PLAY_MC || theMode==MODE_SERVE) {
    theGroup=argv[2];                   // e.g., 239.255.255.250 for SSDP
    thePortIn=thePortOut=atoi(argv[3]); // 0 if error, which is an invalid port
    if(!thePortIn) { sprintf(msg,"Invalid inbound/outbound port number '%s'",argv[3]); Usage(msg); return -1; }
  }

  // Parse serve mode options (overwrites outgoing port number)
  //
  if(theMode==MODE_SERVE) {
    thePortOut=atoi(argv[5]); // 0 if error, which is an invalid port
    if(!thePortOut) { sprintf(msg,"Invalid serve mode outbound port number '%s'",argv[5]); Usage(msg); return -1; }

    theDestIP[0]=argv[6];
    if(argc>7)  { theDestIP[1]=argv[7];  }
    if(argc>8)  { theDestIP[2]=argv[8];  }
    if(argc>9)  { theDestIP[3]=argv[9];  }
    if(argc>10) { theDestIP[4]=argv[10]; }
    if(argc>11) { theDestIP[5]=argv[11]; }
    if(argc>12) { theDestIP[6]=argv[12]; }
    if(argc>13) { theDestIP[7]=argv[13]; }
  } 

  // Parse Unicast play options (similar to serve mode)
  //
  if(theMode==MODE_PLAY_UC) {
    thePortOut=atoi(argv[4]); // 0 if error, which is an invalid port
    if(!thePortOut) { sprintf(msg,"Invalid outbound port number '%s'",argv[4]); Usage(msg); return -1; }

    theDestIP[0]=argv[5];
    if(argc>6)  { theDestIP[1]=argv[6];  }
    if(argc>7)  { theDestIP[2]=argv[7];  }
    if(argc>8)  { theDestIP[3]=argv[8];  }
    if(argc>9)  { theDestIP[4]=argv[9]; }
    if(argc>10) { theDestIP[5]=argv[10]; }
    if(argc>11) { theDestIP[6]=argv[11]; }
    if(argc>12) { theDestIP[7]=argv[12]; }
  } 

  // Parse multicast playback interface hardcoding option
  //
  if(theMode==MODE_PLAY_MC && argc==7) { theInterfaceOut=argv[6]; }

  // Print result of parse and return
  //
  if(theMode==MODE_SERVE) {
    fprintf(logfile,"Lets Serve!  %s udp://%s:%d -- serve to port %d at %s",modeStrings[theMode],theGroup,thePortIn,thePortOut,theDestIP[0]);
    if(theDestIP[1])  { fprintf(logfile,", %s",theDestIP[1]);  }
    if(theDestIP[2])  { fprintf(logfile,", %s",theDestIP[2]);  }
    if(theDestIP[3])  { fprintf(logfile,", %s",theDestIP[3]);  }
    if(theDestIP[4])  { fprintf(logfile,", %s",theDestIP[4]);  }
    if(theDestIP[5])  { fprintf(logfile,", %s",theDestIP[5]);  }
    if(theDestIP[6])  { fprintf(logfile,", %s",theDestIP[6]);  }
    if(theDestIP[7])  { fprintf(logfile,", %s",theDestIP[7]);  }
    fprintf(logfile,"\n");
  } else if(theMode==MODE_PLAY_UC) {
    fprintf(logfile,"Lets Play Unicast!  %s %s -- send to port %d at %s",modeStrings[theMode],theFileName,thePortOut,theDestIP[0]);
    if(theDestIP[1])  { fprintf(logfile,", %s",theDestIP[1]);  }
    if(theDestIP[2])  { fprintf(logfile,", %s",theDestIP[2]);  }
    if(theDestIP[3])  { fprintf(logfile,", %s",theDestIP[3]);  }
    if(theDestIP[4])  { fprintf(logfile,", %s",theDestIP[4]);  }
    if(theDestIP[5])  { fprintf(logfile,", %s",theDestIP[5]);  }
    if(theDestIP[6])  { fprintf(logfile,", %s",theDestIP[6]);  }
    if(theDestIP[7])  { fprintf(logfile,", %s",theDestIP[7]);  }
    fprintf(logfile,"\n");
  } else if(theMode==MODE_RECORD) {
    fprintf(logfile,"Lets Record!  %s %s udp://%s:%d\n",modeStrings[theMode],theFileName,theGroup,thePortIn);
  } else if(theMode==MODE_PLAY_MC) {
    fprintf(logfile,"Lets Play!  %s %s udp://%s:%d",modeStrings[theMode],theFileName,theGroup,thePortOut);
    if(theInterfaceOut) { fprintf(logfile," interface %s",theInterfaceOut); }
    fprintf(logfile,"\n");
  } else {
    fprintf(logfile,"Bad Mode: %d\n",theMode);
    return -1;
  }
  return 0;
}

// for unicast output modes "connect" to the destinations.  It is UDP so does not really connect
// but it makes for more efficient packet sending later via send() vs sendto()
//
void serve_setup() {
  int i,ret;

  // set up destination addresses
  //
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(thePortOut);

  // loop thru ip strings
  //
  for(i=0;theDestIP[i];i++) {
    addr.sin_addr.s_addr = inet_addr(theDestIP[i]);                       // set IP
    theDestSock[i]=socket(AF_INET, SOCK_DGRAM, 0);                        // create socket
    ret=connect(theDestSock[i], (struct sockaddr *)&addr, sizeof(addr));  // connect
    fprintf(logfile,"Connect %s status %d\n",theDestIP[i],ret);           // report

    // error handle
    //
    if(ret<0) {
      fprintf(logfile,"FATAL!  ERROR CONNECTING\n");
      _exit(-1);
    }
  }
}

// Receive multicast channel and save it to file or send it back out via unicast
//
int recorder(int fd) {
    unsigned int firstTick=0;
    packetType packet;
    int tgt;
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

      // note timestamp for recording and/or status logging
      //
      packet.tick=getTicks();
      if(!firstTick) { firstTick=packet.tick; }
      packet.tick=packet.tick-firstTick;

      // note payload size
      //
      packet.payloadSz=(unsigned int)nbytes;

      if(theFile) {
        //
        // Record mode.  write payload structures out to file
        //
        fwrite(&packet,1,packet.payloadSz+8,theFile);
      } else {
        //
        // Serve mode.  unicast payloads to targets
        //
        for(tgt=0;theDestSock[tgt];tgt++) {
          send(theDestSock[tgt],packet.payload,packet.payloadSz,0);
        }
      }
      packetCt++;
      netBytes=netBytes+nbytes;
      if(packetCt%500==1) {
        fprintf(logfile,"t=%u ct=%d netb=%d b=%d\n",packet.tick,packetCt,netBytes,packet.payloadSz);
      }
  }
  return 0;
}

// Open a previously saved file send it out via unicast or multicast
//
int player(int fd) {
  unsigned int tick,firstTick=0;
  packetType packet;
  int packetCt=0;
  int dataPacketCt=0;
  int zeroPacketCt=0;
  int netBytes=0;
  int tgt,nbytes;
  struct sockaddr_in addrMC;

  if(theMode==MODE_PLAY_MC) {
    // set up destination address
    //
    memset(&addrMC, 0, sizeof(addrMC));
    addrMC.sin_family = AF_INET;
    addrMC.sin_addr.s_addr = inet_addr(theGroup);
    addrMC.sin_port = htons(thePortOut);
  
    // 
    // Specific interface for multicast https://stackoverflow.com/questions/9701561/how-to-specify-the-multicast-send-interface-in-python
    //
    if(theInterfaceOut) {
      unsigned long sa=inet_addr(theInterfaceOut);
      int tmp=setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, (char*) &sa, sizeof(sa));
      if(setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, (char*) &sa, sizeof(sa)) < 0) {
#ifdef _WIN32
        fprintf(logfile,"Error initializing custom interface : WinSock Error: %d\n",WSAGetLastError());
#else
        fprintf(logfile,"Error initializing custom interface : Errno Error: %d\n",errno);
#endif
        return 1;
      }
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
        usleep((packet.tick-toffset)*1000);
#endif
      }

      // It is time.  Lets send it
      //
      if(theMode==MODE_PLAY_MC) {
        //
        // Multicast Mode
        //
        nbytes = sendto(fd, packet.payload, packet.payloadSz, 0, (struct sockaddr*) &addrMC, sizeof(addrMC));
        if (nbytes < 0) {
          perror("sendto");
          return 1;
        }
      } else {
        //
        // Unicast mode.  unicast payloads to targets
        //
        for(tgt=0;theDestSock[tgt];tgt++) {
          nbytes=send(theDestSock[tgt],packet.payload,packet.payloadSz,0);
          if (nbytes < 0) { perror("sendto"); return 1; }
        }
      }
      dataPacketCt++;
      netBytes=netBytes+packet.payloadSz;
    } else {
      zeroPacketCt++;
    }
    packetCt++;
    if(packetCt%500==1) {
      fprintf(logfile,"t=%u dct=%d zct=%d netb=%dd\n",packet.tick,dataPacketCt,zeroPacketCt,netBytes);
    }

    // read next header
    fread(&packet,1,8,theFile);
  }
  fprintf(logfile,"EOF.  Wrote %d bytes from %d data packets. skipped %d zero packets\n",netBytes,packetCt,zeroPacketCt);
  return 0;
}

int main(int argc, char *argv[]) {
    char msg[1024];
    logfile=stderr;
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

    if(theMode==MODE_RECORD)  { recorder(fd); }
    if(theMode==MODE_PLAY_MC) { player(fd);   }
    if(theMode==MODE_PLAY_UC) { serve_setup(); player(fd);   }
    if(theMode==MODE_SERVE)   { serve_setup(); recorder(fd); }

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

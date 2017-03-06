/*
 * lxi-control - A command line client for controlling LXI devices
 *
 * This application allows the sending of Standard Commands for Programable 
 * Instruments (SCPI).
 *
 * Author: Martin Lund (mgl@doredevelopment.dk)
 *
 * Copyright 2009 DoréDevelopment
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/file.h>

#include <stdint.h>

/* Application configuration */
#define APP_VERSION		"1.0.0"
#define NET_NODELAY	1	// TCP nodelay enabled
#define NET_TIMEOUT	4	// Network timeout in seconds (default)
#define NET_MAX_NODES	256
#define ERR		-1
#define NET_MAX_BUF	1500
#define BROADCAST_ADDR	"255.255.255.255"
#define BROADCAST_PORT	111

/* Status message macros */
#define INFO(format, args...) \
	fprintf (stdout, "" format, ## args)

#define ERROR(format, args...) \
	fprintf (stderr, "Error: " format, ## args)

#define MODE_NORMAL	0
#define MODE_DISCOVERY	1

/*Waveform globals*/
//char * waveform_buf;
uint8_t * w_buf;
int32_t * waveform_buf;
//int16_t * waveform_buf;
int32_t * net_wave_buf;
int16_t * another_buf;
long lSize;
FILE * file;
bool wf = false;

/* Configuration structure */
static struct {
	char *ip;		/* Instrument IP */
	unsigned int port;	/* Instrument port number */
	char *command;		/* SCPI command */
	int socket;		/* Socket handle */
	int mode;		/* Program mode */
	int timeout;
} config = {			/* Defaults */
	NULL,
	9221,
	NULL,
	0,
	MODE_NORMAL,
	NET_TIMEOUT
};

/* Binary UDP payload which represents GETPORT RPC call */
char rpc_GETPORT_msg[] = {
0x00, 0x00, 0x03, 0xe8, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x86, 0xa0, 
0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x06, 0x07, 0xaf, 0x00, 0x00, 0x00, 0x01, 
0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00 };

/*----------------------------------------------------------------------------*/

void print_help(void)
{
	INFO("Usage: lxi-control [options]\n");
	INFO("\n");
	INFO("Options:\n");
	INFO("--ip       <ip>       Remote device IP\n");
	INFO("--port     <port>     Remote device port (default: %d)\n",
								config.port);
	INFO("--scpi     <command>  SCPI command\n");
	INFO("--timeout  <seconds>  Network timeout (default: %d s)\n",
								config.timeout);
	INFO("--discover            Discover LXI devices on hosts subnet\n");
	INFO("--version             Display version\n");
	INFO("--help                Display help\n");
	INFO("\n");
}
static int parse_options(int argc, char *argv[])
{
	static int c;
  /* // These are now globals
  unsigned char * waveform_buf;
  long lSize;
  FILE * file;
  */
	/* Print usage help if no arguments */
	if (argc == 1)
	{
		print_help();
		exit(1);
	}

	while (1)
	{
		static struct option long_options[] =
		{
			{"ip",		required_argument,	0, 'i'},
			{"port",	required_argument,	0, 'p'},
			{"scpi",	required_argument,	0, 's'},
			{"file",	required_argument,	0, 'f'},
			{"timeout",	no_argument,		0, 't'},
			{"discover",	no_argument,		0, 'd'},
			{"version",	no_argument,		0, 'v'},
			{"help",	no_argument,		0, 'h'},
			{0, 0, 0, 0}
		};
		
		/* getopt_long stores the option index here. */
		int option_index = 0;

		/* Parse argument using getopt_long (no short opts allowed) */
		c = getopt_long (argc, argv, "", long_options, &option_index);

		/* Detect the end of the options. */
		if (c == -1)
			break;

		switch (c)
		{
			case 0:
				/* If this option set a flag, do nothing else now. */
				if (long_options[option_index].flag != 0)
					break;
				INFO("option %s", long_options[option_index].name);
				if (optarg)
					INFO(" with arg %s", optarg);
				INFO("\n");
				break;
			
			case 'i':
				config.ip = optarg;
				break;

			case 'p':
				config.port = atoi(optarg);
				break;

			case 's':
				config.command = optarg;
        //int t;
        //if(t=strcmp(optarg, "*IDN?") == 0) fprintf(stdout, "Read ID:\n");
        //fprintf(stdout, "optarg: %s strcmp: %d\n", optarg, t);
        /*
        char * yo;
        int n;
        long int siz = 1234;
        yo = calloc(1, 30);
        //strcpy(yo, lSize);
        n = sprintf(yo, "%ld", siz);
        fprintf(stdout, "tall: %s, er bestaar av tegn: %d\n", yo, n);
        */
				break;
			
      case 'f':
        printf("file: %s\n", optarg);
				file = fopen(optarg, "rb");
        if (file == NULL){
          fprintf(stdout, "Error opening file %s\n", optarg);
          exit(1);
        }
        
        fseek(file, 0L, SEEK_END);
        lSize = ftell(file);
        rewind(file);
        printf("waveform size: %ld\n", lSize);

        waveform_buf = (int32_t*) calloc((lSize/4), sizeof(int32_t));
        w_buf = (int8_t*) calloc(lSize, sizeof(int8_t));
        if(!waveform_buf) {
          fclose(file);
          fprintf(stdout, "Memory allocation failed\n");
          exit(-1);
        }
        size_t res;
   //     printf("siesoso: %d\n",sizeof(uint32_t));
//        res = fread(waveform_buf, sizeof(uint32_t), lSize, file); // lSize:num_bytes
        res = fread(waveform_buf, 1, lSize, file); // lSize:num_bytes
        printf("waveform_buf:\n");
        printf("res: %ld\n", res);
        printf("buf: %d %d\n", waveform_buf[0], waveform_buf[1]);
        printf("buf: 0x%x 0x%x\n", waveform_buf[0], waveform_buf[1]);
        if(res != lSize){
          fclose(file);
          free(waveform_buf);
          fprintf(stdout, "Failed to read file into buffer -- res: %ld, lSize: %ld\n", res, lSize);
          exit(-1);
        }
        /* 8bit */
        rewind(file);
        res = fread(w_buf, sizeof(int8_t), lSize, file); // lSize:num_bytes
        printf("w_buf:\n");
        printf("res: %ld\n", res);
        printf("buf: %d %d\n", w_buf[0], w_buf[1]);
        printf("buf: 0x%x 0x%x 0x%x\n", w_buf[0], w_buf[1], w_buf[2]);
        if(res != lSize){
          fclose(file);
          free(w_buf);
          fprintf(stdout, "Failed to read file into buffer -- res: %ld, lSize: %ld\n", res, lSize);
          exit(-1);
        }

        /* 16bit */
        another_buf = (int16_t*) calloc((lSize/2), sizeof(int16_t));
        rewind(file);
        res = fread(another_buf, sizeof(int16_t), lSize, file); // lSize:num_bytes
        printf("another_buf:\n");
        printf("res: %ld\n", res);
        printf("buf: %i %i\n", another_buf[0]&0xffff, another_buf[1]&0xffff);
        printf("buf: 0x%x 0x%x 0x%x\n", another_buf[0]&0xffff, another_buf[1]&0xffff, another_buf[2]&0xffff);
        printf("buf3: 0x%x 0x%x 0x%x\n", another_buf[0]&0x7fff, another_buf[1]&0x7fff, another_buf[2]&0x7fff);
        printf("buf4: 0x%x 0x%x 0x%x\n", (another_buf[0]+8192)&0x7fff, (another_buf[1]+8192)&0x7fff, (another_buf[2]+8192)&0x7fff);
        printf("buf5: 0x%x 0x%x 0x%x\n", (another_buf[0]&0x7fff)+8192, (another_buf[1]&0x7fff)+8192, (another_buf[2]&0x7fff)+8192);
        printf("buf6: 0x%x 0x%x 0x%x\n", another_buf[1024], another_buf[1025], another_buf[1026]);
        printf("buf6: 0x%x 0x%x 0x%x\n", (another_buf[1024]+8192)&0x7fff, (another_buf[1025]+8192)&0x7fff, (another_buf[1026]+8192)&0x7fff);
        printf("buf7: 0x%x 0x%x 0x%x\n", (another_buf[1024]&0x7fff)+8192, (another_buf[1025]&0x7fff)+8192, (another_buf[1026]&0x7fff)+8192);
        if(res != lSize/2){
          fclose(file);
          free(another_buf);
          fprintf(stdout, "Failed to read file into buffer -- res: %ld, lSize: %ld\n", res, lSize);
          exit(-1);
        }
        /* Have to hack the shitty output of TTi's waveform editor */
        int i;
        int32_t temp;
        for(i=0; i<lSize/2; i++){
          /* Amplitude goes from -8192 to +8192, but -8192 is 0 in the generator
           * only the 14 LSB bits are used.
           * */
          temp = (another_buf[i] + 8192)&0x7fff; 
          another_buf[i] = (uint16_t)temp;
        }

        //exit(0);
        #if 0
        net_wave_buf = (int32_t*) calloc((lSize/4), sizeof(int32_t));
        int i;
        int j = 0;
        uint32_t temp;
        for(i=0; i<lSize/4; i++,j+=2){
          //net_wave_buf[i] = htonl(waveform_buf[i]);
          temp = another_buf[i] << 16;
          temp |= another_buf[i+1] & 0xffff;
          net_wave_buf[i] = htonl(temp);
//          net_wave_buf[i] = temp;
//          net_wave_buf[i] = htonl(waveform_buf[i]);
//          printf("i: %d, j: %d \n", i, j);
//          printf("temp: 0x%x, buf: 0x%x, wf_buf: 0x%x\n", temp, net_wave_buf[i], ntohl(waveform_buf[i]));
        }
        #endif
//        exit(0); // testing
        if(strcmp(config.command,"ARB1") ||
           strcmp(config.command,"ARB2") ||
           strcmp(config.command,"ARB3") ||
           strcmp(config.command,"ARB4") ){
          /* We want to define waveform */
          wf=true;
        }
        /*
        int nn;
        nn = sprintf(NULL, "%s", waveform_buf);
        */
        printf("wf: %d\n", wf);
        printf("wf buffer size: %ld\nwaveform_buf: %x\n", sizeof(waveform_buf), waveform_buf[2]);
        //fclose(file); /*Remember to free buffer when done*/
        printf("closed file\n");
				break;

			case 'v':
				INFO("lxi-control v%s\n", APP_VERSION);
				exit(0);
				break;

			case 'd':
				config.mode = MODE_DISCOVERY;
				break;

			case 'h':
				print_help();
				exit(0);
				break;

			case '?':
				/* getopt_long already printed an error message. */
				break;

			default:
				ERROR("End of options\n");
				exit(1);
		}
	}

	/* Check that --ip is set */
	if ((config.ip == NULL) && (config.mode == MODE_NORMAL))
	{
		ERROR("Missing option: --ip\n");
		exit(1);
	}

	/* Print any remaining command line arguments (invalid options). */
	if (optind < argc)
	{
		ERROR("%s: unknown arguments: ", argv[0]);
		while (optind < argc)
			ERROR("%s ", argv[optind++]);
		ERROR("\n");
		exit(1);
	}
}

static int disconnect_instrument(void)
{
	/* Close socket */
	if(close(config.socket)==ERR)
	{
		ERROR("Error closing socket\n");
		exit(3);
	}
	
	return 0;
}

static int connect_instrument(void)
{
	int retval = 0;
	struct sockaddr_in recv_addr = { 0 };
	int state_nodelay = NET_NODELAY;
	struct timeval tv;
	
	/* Create socket */
	config.socket = socket(PF_INET,SOCK_STREAM,0);
	
	if(config.socket == ERR)
	{
		ERROR("Error connecting socket: %s\n",strerror(errno));
		exit(3);
	}

	/* Set socket options - TCP */
	setsockopt(config.socket, IPPROTO_TCP, TCP_NODELAY,
				(void *)&state_nodelay,sizeof state_nodelay);

	/* Set socket options - timeout */
	tv.tv_sec = config.timeout;
	tv.tv_usec = 0;
	if ((setsockopt(config.socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))) == ERR)
	{
		perror("setsockopt - SO_RCVTIMEO");
		exit(3);
	}

	/* Receivers address */
	memset(&recv_addr,0,sizeof(struct sockaddr_in));
	recv_addr.sin_family=PF_INET;			// IPv4
	recv_addr.sin_port=htons(config.port);
	inet_aton(config.ip, &recv_addr.sin_addr);	// IP Address

	/* Establish TCP connection */
	retval = connect(config.socket,(struct sockaddr *)&recv_addr,
						sizeof(struct sockaddr_in));
	if(retval == ERR)
	{
		ERROR("Error establishing TCP connection: %s\n",strerror(errno)); // FIXME
	}
	return retval;
}

static int send_command(void)
{
	int retval = 0;
  int c;
  printf("send_command\n");
  if(!wf){
  //#if 0
    char * newBuff = (char*) calloc(1,strlen(config.command)+1+1); /* make room for one LF and NULL*/
    strcpy(newBuff, config.command);
    strcat(newBuff, "\n");
    config.command = newBuff;
  //#endif
  #if 0
    int8_t * newBuff = (int8_t*) calloc(1,strlen(config.command)+1); /* make room for one LF and NULL*/
    for(c = 0; c<strlen(config.command); c++){
      newBuff[c] = config.command[c];
      printf("c: %d, newBuff[c]: %c \n", c, newBuff[c]);
    }
    newBuff[c] = 0x0A;
      printf("c: %d, newBuff[c]: %c \n", c, newBuff[c]);
    char * new = (char*)newBuff;
    printf("casted: %s\n", new);
    int32_t * te = (int32_t *) calloc(1, (strlen(config.command)/4)+1);
    for(c=0; c<4*strlen(config.command); c+=4){
      int32_t temp;
      temp = newBuff[c] | newBuff[c+1]<<8 | newBuff[c+2]<<16 | newBuff[c+3]<<24;
      if(c>3){
        te[c-4] = htonl(temp);
      } else {
        te[c] = htonl(temp);
      }
    }
    #endif
  	/* Send SCPI command */
	  //retval = send(config.socket,&newBuff,strlen(config.command)+1,0);
	  retval = send(config.socket,config.command,strlen(config.command),0);
    printf("strlen: %d\n", strlen(config.command));
	  if (retval == ERR) {
		  ERROR("Error sending SCPI command\n");
		  exit(3);
	  }
  // Waveform loading
  } else {
    printf("else\n");
    int h_num; /* Storing number of chars in the size */
    int h_size;
    h_num = snprintf(NULL, 0, "%ld", lSize); /*Count chars in lSize*/
    printf("h_num: %d\n", h_num);
    char * header = (char *) calloc(1, 3+h_num+1); /* <space>+#+number of chars to follow + h_num + 0 */
    h_size = sprintf(header, " #%d%ld", h_num, lSize); /*Assemble header, space between command and data is defined here*/
    printf("h_size: %d, header: %s\n", h_size, header);

#if 0 // convert everything to uin8_t
    /*Send command and header*/
    uint8_t * nBuff = (uint8_t*) calloc(1,strlen(config.command)+strlen(header)+1); /* make room for LF(0x0A)*/
//    strcpy(newBuff, config.command);
//    strcat(newBuff, header);
//    char LF[2] = {'\n','\0'};
    //int c;
    int cLength = strlen(config.command)+strlen(header)+lSize+1;
    uint8_t * sCommand = (uint8_t*)calloc(1,cLength);
    for(c = 0; c < cLength; c++){
      if(c<strlen(config.command)){
        sCommand[c] = config.command[c];
      }
      if(c>=strlen(config.command) && c<strlen(header)){
        sCommand[c] = header[c];
      }
      if( (c>=(strlen(header)+strlen(config.command))) && 
           c<cLength-1 ){
        sCommand[c] = waveform_buf[c];
      }
      if(c==cLength-1){
        printf("LF c: %d\n", c);
        sCommand[c] = 0x0A; /* LF */
      }
    }
    printf("end c: %d\n", c);
    /* Send SCPI command */
    retval = send(config.socket, sCommand, cLength, 0);
    if (retval == ERR)
    {
      ERROR("Error sending SCPI command\n");
      exit(3);
    }   
#endif

//#if 0
    char * newBuff = (char*) calloc(1,strlen(config.command)+1); /* make room for one NULL*/
    strcpy(newBuff, config.command);
    config.command = newBuff;
    //printf("starting conversion\n");
	  retval = send(config.socket,config.command,strlen(config.command),0);
	  if (retval == ERR) {
		  ERROR("Error sending SCPI command\n");
		  exit(3);
	  }
    printf("command: %s, retval=%d\n", config.command, retval);
    /* Send header */
	  retval = send(config.socket,header,h_size,0);
	  if (retval == ERR) {
		  ERROR("Error sending SCPI command\n");
		  exit(3);
	  }
    printf("header:%s, retval=%d, h_size=%d\n", header, retval, h_size);

    /*Send bytes*/
    printf("sizeof(waveform_buf) size: %d, lSize: %ld\n", sizeof(waveform_buf), lSize);
    printf("sizeof(uint32_t): %d\n", sizeof(uint32_t));
    printf("sizeof(uint32_t)*lSize: %d\n", sizeof(uint32_t)*lSize);
	 // retval = send(config.socket,waveform_buf,sizeof(uint32_t)*lSize/4,0);
	  //retval = send(config.socket,net_wave_buf,lSize,0);
    //

    int16_t * test = (int16_t*)calloc(lSize/2, sizeof(int16_t));
    int g;
    int16_t temp;
    for(g=0; g<lSize/2; g++){
      temp = (another_buf[g]<<8)&0xff00;
      temp |= (another_buf[g]>>8)&0xff;
      another_buf[g]=temp;
    }

	  retval = send(config.socket,another_buf,lSize,0);
//	  retval = send(config.socket,net_wave_buf,lSize,0);
//	  retval = send(config.socket,w_buf,lSize,0);
	  if (retval == ERR) {
      printf("Error\n");
		  ERROR("Error sending SCPI command\n");
		  exit(3);
	  }
    printf("waveform_buf: retval=%d\n", retval);

    /*Send LF*/
    char LF[2] = {'\n','\0'};
	  retval = send(config.socket,LF,strlen(LF),0);
	  if (retval == ERR) {
		  ERROR("Error sending SCPI command\n");
		  exit(3);
	  }
    printf("LF: %s, retval=%d\n", LF, retval);

//#endif
#if 0
    int c;
    char temp[3];
    char * buff = (char*) calloc(buff, lSize+1);
    /*Do some conversion bullshit*/
    for(c=0; c<lSize/2; c++){
      //itoa(waveform_buf[c], temp, 10);
      //sprintf(&temp, "%d",waveform_buf[c]);
      temp[0] = waveform_buf[c] >> 8;
      temp[1] = waveform_buf[c]&0xFF;
      //temp[0] = waveform_buf[c]&0xFF;
      if(temp[0] == NULL && temp[1] == NULL){
        printf("temp[0]: %c, temp[1]: %c\n", temp[0], temp[1]);
      }
      //temp[1] = waveform_buf[c]>>8;
      temp[3] = '\0';
//      printf("%s",temp);
      strcat(newBuff, temp);
      strcat(buff, temp);
    }
    printf("buff length: %ld", strlen(buff));
//    strcat(newBuff, waveform_buf);
    strcat(newBuff, "\n");
    config.command = newBuff;
    printf("config.command size: %d\n", strlen(config.command));
    //free(waveform_buf);
//    free(newBuff);
//    printf("command:\n%s\n",config.command);
#endif
  }
#if 0
	/* Add string termination to command */
	config.command[strlen(config.command)] = 0;
#endif
	#if 0
  /* Send SCPI command */
	retval = send(config.socket,config.command,strlen(config.command)+1,0);
	if (retval == ERR)
	{
		ERROR("Error sending SCPI command\n");
		exit(3);
	}
  #endif
	return retval;
}

static int receive_response(void)
{
	char buffer[189500];
	int length;
	int i, question = 0;
	fd_set rset;
	int ret;
	struct timespec t;
	
	/* Skip receive if no '?' in command */
	for (i=0; i<strlen(config.command);i++)
	{
		if (config.command[i] == '?')
			question=1;
	}
	if (question == 0)
		return 0;

	/* The device do not return any data if command send is wrong. If no
	 * data recived until the specified timeout, exit. */	
	FD_ZERO(&rset);
	FD_SET(config.socket, &rset);
	t.tv_sec=config.timeout; t.tv_nsec=0;
	ret=pselect(config.socket+1, &rset, NULL, NULL, &t, NULL);
	if(ret == -1) {
		ERROR("Error reading response: %s\n",strerror(errno));
		exit(3);
	}

	if(!ret) {
		INFO("Timeout waiting for response\n");
		exit(2);
	}

	/* Read response */
	if((length=recv(config.socket,&buffer[0],189500,0))==ERR)
	{
		ERROR("Error reading response: %s\n",strerror(errno));
		exit(3);
	}
	
	/* Add zero character (C string) */
	buffer[length]=0; 
	
	/* Print received data */
	printf("%s",buffer);
	
	return 0;
}

static int discover_instruments(void)
{
	int sockfd;
	struct sockaddr_in send_addr;
	struct sockaddr_in recv_addr;
	int broadcast = 1;
	int count;
	int addrlen;
	char buf[NET_MAX_BUF];
	struct timeval tv;
	struct in_addr ip_list[NET_MAX_NODES];
	int i = 0;
	int j = 0;
	char idn_command[] = "*IDN?\n";

	INFO("\nDiscovering LXI devices on hosts subnet - please wait...\n");

	/* create a socket */
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd == -1)
	{
		perror("Socket creation error");
		exit(3);
	}

	/* Set socket options - broadcast */
	if((setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
					&broadcast,sizeof (broadcast))) == ERR)
	{
		perror("setsockopt - SO_SOCKET");
		exit(3);
	}
	
	/* Set socket options - timeout */
	tv.tv_sec = config.timeout;
	tv.tv_usec = 0;
	if ((setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))) == ERR)
	{
		perror("setsockopt - SO_RCVTIMEO");
		exit(3);
	}

	/* Senders address */
	send_addr.sin_family = AF_INET;
	send_addr.sin_addr.s_addr = INADDR_ANY;
	send_addr.sin_port = 0;		// 0 = random sender port

	/* Bind socket to address */
	bind(sockfd, (struct sockaddr*)&send_addr, sizeof(send_addr));

	/* Receivers address */ 
	recv_addr.sin_family = AF_INET;
	recv_addr.sin_addr.s_addr = inet_addr(BROADCAST_ADDR);
	recv_addr.sin_port = htons(BROADCAST_PORT);

	/* Broadcast RPC GETPORT message */
	sendto(sockfd, rpc_GETPORT_msg, sizeof(rpc_GETPORT_msg), 0, 
			(struct sockaddr*)&recv_addr, sizeof(recv_addr));

	addrlen = sizeof(recv_addr);

	/* Go through received responses */
	do {
		count = recvfrom(sockfd, buf, NET_MAX_BUF, 0, 
					(struct sockaddr*)&recv_addr, &addrlen);
		if (count > 0)
		{
			ip_list[i] = recv_addr.sin_addr;
			i++;
		}
	} while (count > 0);

	INFO("\nDiscovered devices:\n");

	/* Request SCPI IDN of responding hosts */
	for (j=0; j<i; j++)
	{
		config.ip = inet_ntoa(ip_list[j]);
		if (connect_instrument() == 0)
		{
			config.command = idn_command;
			
			send_command();
			
			INFO("IP %s  -  ", config.ip);
			
			receive_response();
		
			disconnect_instrument();
		}
	}

	INFO("\n");

	return 0;
}

int main (int argc, char *argv[])
{
	/* Parse command line options */
	parse_options(argc, argv);
	
	if (config.mode == MODE_DISCOVERY)
	{
		/* Discover instruments IPs via VXI-11 broadcast */
		discover_instruments();
	}
	else
	{
	
		/* Connect instrument */
		if (connect_instrument())
			exit(2);
	
		/* Send command */
		send_command();
	
		/* Read response */
		receive_response();
	
		/* Disconnect instrument */
		disconnect_instrument();
	}
	
	exit (0);
}

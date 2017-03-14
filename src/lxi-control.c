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
 * Modified: Lars Bratrud (lars.bratrud@cern.ch), 2017
 * Added support for controlling TTi TG5011 Function Generator
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
#include <netdb.h> // hostent
#include <stdint.h>
#include <unistd.h> // implicit decl of close 

/* Application configuration */
#define APP_VERSION		"1.1.0"
#define NET_NODELAY	1	// TCP nodelay enabled
#define NET_TIMEOUT	4	// Network timeout in seconds (default)
#define NET_MAX_NODES	256
#define ERR		-1
#define NET_MAX_BUF	1500
#define BROADCAST_ADDR	"255.255.255.255"
#define BROADCAST_PORT	111

#define MAX_WF_BUFFER 128*1024*2 // 128k points of 2 bytes each

/* Status message macros */
#define INFO(format, args...) \
	fprintf (stdout, "" format, ## args)

#define ERROR(format, args...) \
	fprintf (stderr, "Error: " format, ## args)

#define MODE_NORMAL	0
#define MODE_DISCOVERY	1

//bool debug = true;
bool debug = false;

/*Waveform globals*/
int16_t * waveform_buf;
long lSize;
FILE * file;
FILE * outFile;
char * fileNameOut;
//int16_t * read_buf;
bool wf = false;
bool fitWaveform = false;
int waveAmplitude; // Default from Waveform Manager
int genAmplitude = 8192; // Function generator peak amplitude
int fileAmp=0;
int customAmp=0; // Function generator peak amplitude
bool usingCustomAmp = false;
bool getWaveData = false;

/* Plotting */
bool dumpPlot = false;
char * plotFileName;
bool plotNameSpecified = false;

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

/* Waveform information */
typedef struct {
  char name[40];         /* Waveform name */
  char interpolation[4]; /* Interpolation ON or OFF */
  int  length;           /* Number of points */
  int  nBytes;           /* Number of bytes */
  int  arb;              /* Waveform number (ARB<arb>) */
} wf_info_t;

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

int hostname_to_ip(char *  , char *);

void print_help(void)
{
  INFO("----------------------------------------------------------------------------------------------\n");
	INFO("Usage: lxi-control [options]\n");
  INFO("----------------------------------------------------------------------------------------------\n");
	INFO("\n");
	INFO("Options:\n");
	INFO("--ip,i       <ip>           Remote device IP\n");
	INFO("--host,n     <host name>    Remote device host name\n");
	INFO("--port,p     <port>         Remote device port (default: %d)\n",
								config.port);
	INFO("--scpi,s     <command>      SCPI command. Commands are not case sensitive\n");
	INFO("--file,f     <filename>     Waveform filename\n");
	INFO("--gnuplot,g  <filename>     Plot waveform in gnuplot and dump to file (in home folder)\n"
       "                            (default is name given in function generator)\n");
	INFO("--adjust,a   <amp>          Adjust waveform to fit original peak amplitude <amp> \n"
       "                            to function generator max peak amplitude of 8192 counts.\n"
       "                            Default value is read from first 2 bytes of .wfm file\n");
	INFO("--timeout,t  <seconds>      Network timeout (default: %d s)\n",
								config.timeout);
	INFO("--discover,d                Discover LXI devices on hosts subnet\n");
	INFO("--version,v                 Display version\n");
	INFO("--help,h                    Display help\n");
	INFO("\n\n");
  /* Print some examples */
  INFO("----------------------------------------------------------------------------------------------\n");
  INFO("Examples:\n");
  INFO("----------------------------------------------------------------------------------------------\n");
  INFO("* Fetch waveform stored in ARB1, store to ~/test2.out and create a png of the waveform with default name:\n");
  INFO("\t./lxi-control --host functiongenerator.cern.ch --scpi arb1? --file ~/test2.out -g\n\n");
  INFO("* Load waveform to function generator:\n");
  INFO("\n");
}
static int parse_options(int argc, char *argv[])
{
	static int c;
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
			{"ip",		  required_argument,	0, 'i'},
			{"host",		required_argument,	0, 'n'},
			{"port",	  required_argument,	0, 'p'},
			{"scpi",	  required_argument,	0, 's'},
			{"file",	  required_argument,	0, 'f'},
			{"gnuplot", optional_argument,	0, 'g'},
			{"adjust",	optional_argument,	0, 'a'},
			{"timeout",	no_argument,		    0, 't'},
			{"discover",no_argument,		    0, 'd'},
			{"version",	no_argument,		    0, 'v'},
			{"help",	  no_argument,		    0, 'h'},
			{0, 0, 0, 0}
		};
		
		/* getopt_long stores the option index here. */
		int option_index = 0;

		/* Parse argument using getopt_long (no short opts allowed) */
		c = getopt_long (argc, argv, "i:n:p:s:f:g::a::tdvh", long_options, &option_index);
		//c = getopt_long (argc, argv, "i:n:p:s:f:a:t:d:v:h:", long_options, &option_index);

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
			
      /* Define IP */
			case 'i':
				config.ip = optarg;
				break;
			
      /* Get IP from hostname */
      case 'n':
        {};
        char test[100];
        if(hostname_to_ip(optarg, test)!=0){
          printf("Could not resolve %s, exiting...\n", optarg);
          exit(1);
        } else {
					config.ip = test;
          //strcpy(config.ip, test);
          printf("Resolved %s to ip %s\n",optarg, config.ip);
        }
				break;

      /* Configure port number */
			case 'p':
				config.port = atoi(optarg);
				break;

      /* Set command */
			case 's':
				config.command = optarg;
				break;
		
      /* fit */
      case 'a':
        fitWaveform = true;
        if(optarg) {
          customAmp = (int)atoi(optarg);
          if(customAmp==0) {
            printf("Zero amplitude is impossible, exiting...\n");
            exit(1);
          }
          usingCustomAmp = true;
          if(debug) printf("customAmp: %d\n",customAmp);
        } else {
          usingCustomAmp = false;
        }
        
        if(debug) { 
          printf("Amplitude: ");
          if(optarg) {
            printf("%d\n",customAmp);
          } else {
            printf("not set\n");
          }
        }
        break;

      /* Read waveform file  */
      case 'f':
        /* Check to see if the command is correct */
        if((strncasecmp(config.command,"ARB1",4)==0 ||
            strncasecmp(config.command,"ARB2",4)==0 ||
            strncasecmp(config.command,"ARB3",4)==0 ||
            strncasecmp(config.command,"ARB4",4)==0) 
            ){
          if(debug) printf("ARBx start, strlen:%ld, command: %s, last char: %c\n", strlen(config.command), config.command, config.command[strlen(config.command)-1]);
          
          /* ARBx?  */
          if( (strlen(config.command) == 5) && config.command[strlen(config.command)-1] == '?' ){
            /* We want to fetch waveform */
            getWaveData=true;
            fileNameOut = (char*) calloc(strlen(optarg)+1, sizeof(char));
            memcpy(fileNameOut, optarg, strlen(optarg));
            if(debug) printf("case f, filenameout: %s\n", fileNameOut);

          /* ARBx <bin>  */          
          } else if ( strlen(config.command) == 4 ) {
            /* We want to define waveform */
            wf=true;
            printf("strcmp: %d\n", strncmp(config.command, "ARB1x", 5));
            if(debug) printf("file: %s\n", optarg);
            file = fopen(optarg, "rb");
            if (file == NULL){
              fprintf(stdout, "Read: Error opening file %s, errno: %s\n", optarg,strerror(errno));
              exit(1);
            }
            
            fseek(file, 0L, SEEK_END);
            lSize = ftell(file)-2; // Account for .wfm header
            rewind(file);
            if(debug) printf("waveform size: %ld\n", lSize);
            
            /* 16bit */
            size_t res;
            waveform_buf = (int16_t*) calloc((lSize/2), sizeof(int16_t));
            rewind(file);
            /* Read waveform max peak amplitude from file  */
            res = fread(&fileAmp, sizeof(int16_t), 1, file); // Read wfm header
            if(res != 1){
              fclose(file);
              free(waveform_buf);
              printf("Could not read header in file %s, read %ld bytes\n", optarg, res*2);
            } else {
              fileAmp<0?fileAmp*=-1:fileAmp; // abs(fileAmp)
            }
            
            /* Read waveform data and store in buffer */
            res = fread(waveform_buf, sizeof(int16_t), lSize, file); // lSize:num_bytes
            if(res != lSize/2){
              fclose(file);
              free(waveform_buf);
              fprintf(stdout, "Failed to read file into buffer -- res: %ld, lSize: %ld\n", res, lSize);
              exit(-1);
            } else {
              fclose(file);
              printf("File %s successfully opened, waveform size is %ld points\n", optarg, lSize/2);
            }
            
            /* Normalize and fit the waveform */
            if(fitWaveform){
              if(usingCustomAmp){
                waveAmplitude = customAmp;
                if(debug) printf("using custom amp: %d, waveamp: %d\n", customAmp, waveAmplitude);
              } else {
                waveAmplitude = fileAmp;
                if(debug) printf("using amp read from file: %d, waveamp: %d\n", fileAmp, waveAmplitude);
              }
              /* Have to hack the shitty output of TTi's waveform editor */
              int i;
              uint16_t temp;
              double fTemp;
              for(i=0; i<lSize/2; i++){
                /* Amplitude goes from -8192 to +8192, but -8192 is 0 in the generator
                 * only the 14 LSB bits are used.
                 * */
                fTemp = (double) waveform_buf[i] / (double) waveAmplitude;
                fTemp *= genAmplitude;
                (fTemp >= 0) ? (fTemp+=0.5) : (fTemp-=0.5); // Rounding
                fTemp += genAmplitude;
    //            printf("%f ",fTemp);
                //printf("%d ",(int16_t)fTemp);
                temp = (uint16_t) fTemp;
                waveform_buf[i] = temp & 0x7fff;
              }
            }
         } else {
            printf("File defined but command is not ARBx <bin>, no waveform will be loaded to the function generator\n");
            wf=false;
            printf("strlen: %ld\n", strlen(config.command));
         }
      } else {
        printf("Command not recognized: %s\n", config.command);
      }
				break;

      /* Plot waveform to file  */
      case 'g':
        if(getWaveData){
          if(debug) printf("optarg: %s\n",optarg);
          if(!optarg){
            printf("Plot filename not specified, using name stored in function generator\n");
            //exit(1);
          } else {
            plotFileName = (char*) calloc(strlen(optarg)+1, sizeof(char));
            strcpy(plotFileName, optarg);
            printf("Waveform data from function generator will be stored to: %s \n", plotFileName);
            plotNameSpecified = true;
          }
        }
				break;

      /* Print version */
			case 'v':
				INFO("lxi-control v%s\n", APP_VERSION);
				exit(0);
				break;
  
      /* Discover devices */
			case 'd':
				config.mode = MODE_DISCOVERY;
				break;

      /* Print help */
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
  if(debug) printf("send_command\n");
  if(!wf){
    /* Add <LF> to end of command, this is required by the function generator */
    char * newBuff = (char*) calloc(1,strlen(config.command)+1+1); /* make room for one LF and NULL*/
    strcpy(newBuff, config.command);
    strcat(newBuff, "\n");
    config.command = newBuff;
//    free(newBuff);
  	/* Send SCPI command */
	  retval = send(config.socket,config.command,strlen(config.command),0);
    if(debug) printf("strlen: %ld\n", strlen(config.command));
	  if (retval == ERR) {
		  ERROR("Error sending SCPI command\n");
		  exit(3);
	  }
  // Waveform loading
  } else {
    if(debug) printf("else\n");
    int h_num; /* Storing number of chars in the size */
    int h_size;
    h_num = snprintf(NULL, 0, "%ld", lSize); /*Count chars in lSize*/
    if(debug) printf("h_num: %d\n", h_num);
    char * header = (char *) calloc(1, 3+h_num+1); /* <space>+#+number of chars to follow + h_num + 0 */
    h_size = sprintf(header, " #%d%ld", h_num, lSize); /*Assemble header, space between command and data is defined here*/
    if(debug) printf("h_size: %d, header: %s\n", h_size, header);


    char * newBuff = (char*) calloc(1,strlen(config.command)+1); /* make room for one NULL*/
    strcpy(newBuff, config.command);
    config.command = newBuff;
    //printf("starting conversion\n");
	  retval = send(config.socket,config.command,strlen(config.command),0);
	  if (retval == ERR) {
		  ERROR("Error sending SCPI command\n");
		  exit(3);
	  }
    if(debug) printf("command: %s, retval=%d\n", config.command, retval);
    /* Send header */
	  retval = send(config.socket,header,h_size,0);
	  if (retval == ERR) {
		  ERROR("Error sending SCPI command\n");
		  exit(3);
	  }
    if(debug) printf("header:%s, retval=%d, h_size=%d\n", header, retval, h_size);

    /*Send bytes*/
    if(debug) printf("sizeof(waveform_buf) size: %ld, lSize: %ld\n", sizeof(waveform_buf), lSize);
    if(debug) printf("sizeof(uint32_t): %ld\n", sizeof(uint32_t));
    if(debug) printf("sizeof(uint32_t)*lSize: %ld\n", sizeof(uint32_t)*lSize);
	 // retval = send(config.socket,waveform_buf,sizeof(uint32_t)*lSize/4,0);
	  //retval = send(config.socket,et_wave_buf,lSize,0);

    /*Convert to network order*/
    int g;
    int16_t temp;
    for(g=0; g<lSize/2; g++){
      temp = (waveform_buf[g]<<8)&0xff00;
      temp |= (waveform_buf[g]>>8)&0xff;
      waveform_buf[g]=temp;
    }
	  retval = send(config.socket,waveform_buf,lSize,0);
	  if (retval == ERR) {
      printf("Error\n");
		  ERROR("Error sending SCPI command\n");
		  exit(3);
	  }
    if(debug) printf("waveform_buf: retval=%d\n", retval);

    /*Send LF*/
    char LF[2] = {'\n','\0'};
	  retval = send(config.socket,LF,strlen(LF),0);
	  if (retval == ERR) {
		  ERROR("Error sending SCPI command\n");
		  exit(3);
	  }
    if(debug) printf("LF: %s, retval=%d\n", LF, retval);

  }
	return retval;
}

static int receive_response(char ** response)
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
  printf("length: %d\n", length);
  
  /* Add zero character (C string) */
  buffer[length]=0;
  //response = calloc(length, sizeof(char));
  if(*response != NULL) *response = *&buffer;
  //memcpy(response, buffer, length);
  /* Print received data */
  if(buffer != NULL)
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
		  
			receive_response(NULL);
		
			disconnect_instrument();
		}
	}

	INFO("\n");

	return 0;
}

static int receive_waveform(wf_info_t wf_info)
{
	int i, question = 0;
	fd_set rset;
	int ret;
  long length;
	struct timespec t;
//  int h_num = snprintf(NULL, 0, "%d", nBytes); /*Count chars in nBytes*/
  int h_num = snprintf(NULL, 0, "%d", wf_info.nBytes); /*Count chars in nBytes*/
  int header = 1+1+h_num; /* #+N+nBytes */
  int totalBytes = header+wf_info.nBytes; /* #+N+nBytes+data */
  int bytesLeft = totalBytes;
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

    uint16_t * read_buf;
    int bytesRead=0; 
    /* Read data
     * Has to be read 1426 bytes at a time, first frame is 14267+header
     * */
    //read_buf = (int16_t*) calloc((size/2), sizeof(int16_t));
    read_buf = (uint16_t*) calloc(totalBytes/2, sizeof(uint16_t));

    /* The device first send the first 1432 bytes of data, then the rest in chunks of 1426  */
//    #if 0
    /* Read first chunk */
    if((length=recv(config.socket,read_buf,1432,0))==ERR) {
      ERROR("Error reading response: %s\n",strerror(errno));
      exit(3);
    }
    bytesLeft -= length;
    bytesRead += length;
    /* Read rest of data */
    int bytes;
    for(bytes=1432; bytes<totalBytes; bytes+=length){
      if(bytesLeft>=1426){
        if((length=recv(config.socket,&read_buf[bytes/2],1426,0))==ERR) {
          ERROR("Error reading response: %s\n",strerror(errno));
          exit(3);
        }      
        bytesLeft -= length;
        bytesRead += length;
      } else {
        if((length=recv(config.socket,&read_buf[bytes/2],bytesLeft,0))==ERR) {
          ERROR("Error reading response: %s\n",strerror(errno));
          exit(3);
        }       
        bytesLeft -= length;
        bytesRead += length;
      }
    }

    /* Convert to host endianness */
    uint16_t * netToHost = (uint16_t*) calloc(wf_info.nBytes/2, sizeof(uint16_t));
    for(i=header; i<(bytesRead-header)/2; i++) netToHost[i] = ntohs(read_buf[i]);
    
    /* Plot test */
    if(!plotNameSpecified){
      plotFileName = (char*) calloc(strlen(wf_info.name)+4+1, sizeof(char)); /*Add .png and \0*/
      strcpy(plotFileName, wf_info.name);
      strcat(plotFileName, ".png");
    }
    FILE *gnuplot = popen("gnuplot", "w");
    fprintf(gnuplot, "set term png\n");
    fprintf(gnuplot, "set xlabel 'Bins'\n");
    fprintf(gnuplot, "set ylabel 'Value'\n");
    fprintf(gnuplot, "set title 'Arbitrary waveform ARB%d: %s'\n", wf_info.arb, wf_info.name);
    fprintf(gnuplot, "set output '~/%s'\n", plotFileName);
    fprintf(gnuplot, "plot [0:%d] [0:16384]'-'\n", wf_info.length);
    for(i=0; i<wf_info.nBytes/2; i++){
      fprintf(gnuplot, "%d %d\n", i, netToHost[i]);
    }
    fprintf(gnuplot, "e\n");
/*
    fprintf(gnuplot, "set term png\n");
    fprintf(gnuplot, "set output '%s'\n", plotFileName);
    fprintf(gnuplot, "replot\n");
*/
    fflush(gnuplot);
    printf("Waveform plot stored as ~/%s\n", plotFileName);

    /* Open file for writing */
    outFile = fopen(fileNameOut, "wb");
    if (outFile == NULL){
      fclose(outFile);
      fprintf(stdout, "Error opening file %s, errno: %s\n", fileNameOut, strerror(errno));
      exit(1);
    } else {
       printf("Opened file %s for writing data from function generator\n", fileNameOut);
    }   
    int res;
    /* Dump to file, skip header  */
    res = fwrite(netToHost, sizeof(uint16_t), wf_info.nBytes/2, outFile);
    if(res != wf_info.nBytes/2){
      fclose(outFile);
      free(read_buf);
      free(netToHost);
      printf("Could not write to file, wrote %d bytes, nBytes=%d\n", res*2, wf_info.nBytes);
      exit(1);
    }
    /* Also write a .wfm file */
    FILE * wfmFile;
    char * wfm = (char*) calloc(strlen(fileNameOut)+4+1, sizeof(char));
    strcpy(wfm, fileNameOut);
    strcat(wfm, ".wfm");
    wfmFile = fopen(wfm, "wb");
    if (wfmFile == NULL){
      fclose(outFile);
      fprintf(stdout, "Error opening file %s, errno: %s\n", wfm, strerror(errno));
      exit(1);
    } else {
       printf("Wrote wfm file: %s\n", wfm);
    }
    /* Dump to wfm file  */
    uint16_t wfmHeader = 0x2000;
    res = fwrite(&wfmHeader, sizeof(uint16_t), 1, wfmFile);
    res = fwrite(netToHost, sizeof(uint16_t), wf_info.nBytes/2, wfmFile);
    if(res != wf_info.nBytes/2){
      fclose(wfmFile);
      free(read_buf);
      free(netToHost);
      printf("Could not write to file, wrote %d bytes\n", res*2);
      exit(1);
    }
    printf("Wrote data to files\n");

    /* Free up memory */
    free(plotFileName);
    free(read_buf);
    free(netToHost);
    free(wfm);
    fclose(outFile);
    fclose(wfmFile);
   
    return 0;
}

/* http://www.binarytides.com/hostname-to-ip-address-c-sockets-linux/ */
int hostname_to_ip(char *hostname , char *ip)
{
    int sockfd;  
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_in *h;
    int rv;
 
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;//AF_UNSPEC; // use AF_INET6 to force IPv6
    hints.ai_socktype = SOCK_STREAM;
 
    if ( (rv = getaddrinfo( hostname , NULL , &hints , &servinfo)) != 0) 
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
 
    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) 
    {
        h = (struct sockaddr_in *) p->ai_addr;
        //strcpy(ip , inet_ntoa( h->sin_addr ) );
				memcpy(ip, inet_ntoa(h->sin_addr), strlen(inet_ntoa(h->sin_addr)));
    }
     
    freeaddrinfo(servinfo); // all done with this structure
    return 0;
}


/* MAIN */
int main (int argc, char *argv[])
{

  char * resp;
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
	  if(getWaveData){
      //send_command(arbxdef?);
      //waveforminfo = receive_response();
      wf_info_t wf_info;
      wf_info.arb = (int)config.command[3]-'0'; /* Set arb number */ 
      char * defCommand = calloc(9,1);
      char * oldCommand = calloc(strlen(config.command)+1,1);
      strcpy(oldCommand,config.command);
      strncpy(defCommand,config.command,4);
      strcat(defCommand,"DEF?");
      config.command = defCommand;
      send_command();
      receive_response(&resp);
      if(debug) printf("response: %s\n", resp);
      
      /* extract tokens */
      char name[40]; /* Names should be max 9 chars or it will overflow to the other ARBs */
      char interpolation[4]; /* ON or OFF */
      int nPoints=0; // one point is 2 bytes
      sscanf(resp, "%[^','],%[^','],%d", wf_info.name, wf_info.interpolation, &wf_info.length);
      wf_info.nBytes = 2*wf_info.length;
//      if(debug) printf("name=%s, interpol=%s, nBytes=%d\n", name, interpolation, nPoints);
      if(debug) printf("name=%s, interpol=%s, length=%d, nBytes=%d\n", wf_info.name, wf_info.interpolation, wf_info.length, wf_info.nBytes);

      config.command = oldCommand;
      send_command();
      receive_waveform(wf_info);

    /* Normal command */
    } else {
      /* Send command */
      printf("Sending command: %s\n", config.command);
      send_command();
      /* Read response */
      receive_response(&resp);
  //    printf("response: %s\n", resp);
	  }
		/* Disconnect instrument */
		disconnect_instrument();

    /* Free up */
    free(waveform_buf);
	}
	exit (0);
}

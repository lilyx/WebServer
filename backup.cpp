
const char * usage =
"                                                               \n"
"myhttpd:                                         		        \n"
"                                                               \n"
"Simple server program that shows how to use socket calls       \n"
"in the server side.                                            \n"
"                                                               \n"
"To use it in one window type:                                  \n"
"                                                               \n"
"   myhttpd [-f][-t][-p] <port>          				        \n"
"   Where:  -f: fork -t: threads -p: pool of threads			\n"
"                                                               \n"
"Where 1024 < port < 65536.         						    \n"
"                                                               \n"
"                                                               \n";

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <dirent.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h> 

#ifndef SORT
#define SORT
#include "sort.h"
#endif

#ifndef MODULES
#define MODULES
#include "modules.h"
#endif


int QueueLength = 5;
int port=5555;
char* mode=NULL;
// char acceptH[1024];
// char acceptLanguage[1024];
// char acceptEncoding[1024];
// char cookie[1024];
// char connection[1024];
// char refer[1024];
// char agent[1024];
// char address[1024];
// char portp[1024]; 
// char hostName[1024];
time_t startTime;
int requests;
double maxTime;
char maxFile[1024];
double minTime;
char minFile[1024];
pthread_mutex_t mutex;
pthread_mutex_t timemutex;
pthread_mutex_t logmutex;

// Processes time request
void processTimeRequest( int socket );
void processRequest(int socket);
void iterativeServer(int masterSocket);
void forkServer(int masterSocket);
void threadForEachRequest(int masterSocket);
void poolOfThread(int masterSocket);
void poolSlave(int masterSocket);
void evictNode(Rec** head, Rec* node);
void insertNode(Rec** head, Rec* prevNode, Rec* insertingNode);
void swap (Rec** head, Rec* n1, Rec* n2);
void sort(char* sortby, Rec** list, int num);
void writeStats();
void writeCgi(int fd, char* buf,char* protocol);
void writeDir(Rec* pmyrec, char** htmlHeader, int num,int length);
char* expandPath(char* filePath,char* cwd);
void exeLoadableFile(int fd, char* queryString, char* exefile,char* filePath);
void writeParentLink(char* filePath,char** htmlHeader,char* docPath,char* cwd,int length);
void writeFile(int fd, FILE* readfile, char* documentType,char* protocol);
void writeNotFound(int fd, char* documentType,char* protocol);
void getRequestEndTime(int requestTime,char* docPath);

typedef void (*httprun)(int ssock, char * query_string);
extern "C" void killzombie( int sig )
{
	while(waitpid(-1, NULL, WNOHANG)>0);
}
int
main( int argc, char ** argv )
{
	time(&startTime);
	requests=0;
	maxTime=0;
    minTime=0;
	struct sigaction signalAction1; 
	signalAction1.sa_handler = killzombie; 
    sigemptyset(&signalAction1.sa_mask); 
    signalAction1.sa_flags = SA_RESTART;
    int error1 = sigaction(SIGCHLD, &signalAction1, NULL ); 
    if ( error1 ){
    	perror( "sigaction" );
    	exit( -1 ); 
    }

  // Print usage if not enough arguments
  fprintf( stderr, "%s", usage );
  pthread_mutex_init(&timemutex,NULL);
  pthread_mutex_init(&logmutex,NULL);

 FILE * log=fopen("./http-root-dir/htdocs/logs.html","w");
 fwrite("<html>\n<body>\n<p>\n<h1>Logs</h1><br><table><tr><th>Source Host</th><th>Directory</th><tr><th colspan=\"5\"><hr></th></tr>\n",1,119,log);
 fclose(log);
 
 if(argc==2){
  	if(strstr(argv[1],"-")!=NULL)
		mode=strdup(argv[1]);
	else port = atoi( argv[1] );
  }else if(argc==3){
  	mode=strdup(argv[1]);
  	port = atoi( argv[2] );
  }else if(argc!=1){
  	perror("Invalid argument number");
  	exit(1);
  }

  if(port<1024||port>65536){
  	perror("port");
  	exit(1);
  }
  
  struct sockaddr_in serverIPAddress; 
  // Set the IP address and port for this server
  memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
  serverIPAddress.sin_family = AF_INET;
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  serverIPAddress.sin_port = htons((u_short) port);
  
  // Allocate a socket
  int masterSocket =  socket(PF_INET, SOCK_STREAM, 0);
  if ( masterSocket < 0) {
    perror("socket");
    exit( -1 );
  }

  // Set socket options to reuse port. Otherwise we will
  // have to wait about 2 minutes before reusing the sae port number
  int optval = 1; 
  int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, 
		       (char *) &optval, sizeof( int ) );
   
  // Bind the socket to the IP address and port
  int error = bind( masterSocket,
		    (struct sockaddr *)&serverIPAddress,
		    sizeof(serverIPAddress) );
  if ( error ) {
    perror("bind");
    exit( -1 );
  }
  
  // Put socket in listening mode and set the 
  // size of the queue of unprocessed connections
  error = listen( masterSocket, QueueLength);
  if ( error ) {
    perror("listen");
    exit( -1 );
  }

  if(mode==NULL){
 	   iterativeServer(masterSocket);
	}else if(strcmp(mode, "-f")==0){
		forkServer(masterSocket);
	}else if(strcmp(mode, "-t")==0){
		threadForEachRequest(masterSocket);
	}else if(strcmp(mode, "-p")==0){
		poolOfThread(masterSocket);
	}else{
		perror("mode");
		exit(1);
	}
  

}

void
iterativeServer(int masterSocket){
	
	while ( 1 ) {
		
    	// Accept incoming connections
    	struct sockaddr_in clientIPAddress;
    	int alen = sizeof( clientIPAddress );
    	int slaveSocket = accept( masterSocket,
			      (struct sockaddr *)&clientIPAddress,
			      (socklen_t*)&alen);
		requests++;
		writeStats();
		// strcpy(address,inet_ntoa(clientIPAddress.sin_addr));
// 	    struct hostent * client;
// 		client = gethostbyaddr((const void *)&clientIPAddress.sin_addr,sizeof(clientIPAddress.sin_addr), AF_INET);
// 		strcpy(address,inet_ntoa(clientIPAddress.sin_addr));
// 		sprintf(portp,"%d",clientIPAddress.sin_port);
// 		strcpy(hostName,client->h_name);
		if ( slaveSocket >= 0 ) {
   	 		processRequest( slaveSocket );
		}
  }
}

void
forkServer(int masterSocket){
	while ( 1 ) {
    	// Accept incoming connections
    	struct sockaddr_in clientIPAddress;
    	int alen = sizeof( clientIPAddress );
    	int slaveSocket = accept( masterSocket,
			      (struct sockaddr *)&clientIPAddress,
			      (socklen_t*)&alen);
		if(slaveSocket == -1 && errno == EINTR)
			continue;
			
		requests++;
		writeStats();	      
		
		// struct hostent * client;
// 		client = gethostbyaddr((const char *)&clientIPAddress.sin_addr.s_addr,sizeof(clientIPAddress.sin_addr.s_addr), AF_INET);
// 		strcpy(address,inet_ntoa(clientIPAddress.sin_addr));
// 		sprintf(portp,"%d",clientIPAddress.sin_port);
// 		strcpy(hostName,client->h_name);
		
		if ( slaveSocket >= 0 ) {

    		int ret=fork();
    		if(ret==0){
   	 			processRequest( slaveSocket );
     			exit(0);
			}
			close(slaveSocket);
	
		}
  }
}

void
threadForEachRequest(int masterSocket){
	while ( 1 ) {
    	// Accept incoming connections
    	struct sockaddr_in clientIPAddress;
    	int alen = sizeof( clientIPAddress );
    	
    	int slaveSocket = accept( masterSocket,
			      (struct sockaddr *)&clientIPAddress,
			      (socklen_t*)&alen);
		
		requests++;
		writeStats();			      
		// struct hostent * client;
// 		client = gethostbyaddr((const char *)&clientIPAddress.sin_addr.s_addr,sizeof(clientIPAddress.sin_addr.s_addr), AF_INET);
// 		strcpy(address,inet_ntoa(clientIPAddress.sin_addr));
// 		sprintf(portp,"%d",clientIPAddress.sin_port);
// 		strcpy(hostName,client->h_name);
			      
		if ( slaveSocket >= 0 ) {

    		pthread_t tid;
			pthread_attr_t attr;
			
			pthread_attr_init( &attr );
			pthread_attr_setscope(&attr, PTHREAD_CREATE_DETACHED);

			pthread_create(&tid, &attr,(void * (*)(void *))processRequest, (void *)slaveSocket);
   	 	}
  }
}

void
poolOfThread(int masterSocket){
	pthread_t tid[5];
	pthread_attr_t attr;
	pthread_mutex_init(&mutex,NULL);
	pthread_attr_init( &attr );
	pthread_attr_setscope(&attr, PTHREAD_CREATE_DETACHED);
		
	for(int i=0; i<5; i++){	
		pthread_create(&tid[i], &attr,(void *(*)(void *))poolSlave,(void *)masterSocket);
   	 }
   	 
   	 for(int i=0; i<5; i++){
   	 	pthread_join(tid[i],NULL);
   	 }
   	 
}

void
poolSlave(int masterSocket){
	while(1){
		// Accept incoming connections
    	struct sockaddr_in clientIPAddress;
    	int alen = sizeof( clientIPAddress );
    	pthread_mutex_lock(&mutex);
    	int slaveSocket = accept( masterSocket,
			      (struct sockaddr *)&clientIPAddress,
			      (socklen_t*)&alen);
		requests++;
		writeStats();
		pthread_mutex_unlock(&mutex);

		// struct hostent * client;
// 		client = gethostbyaddr((const char *)&clientIPAddress.sin_addr.s_addr,sizeof(clientIPAddress.sin_addr.s_addr), AF_INET);
// 		strcpy(address,inet_ntoa(clientIPAddress.sin_addr));
// 		sprintf(portp,"%d",clientIPAddress.sin_port);
// 		strcpy(hostName,client->h_name);

		if ( slaveSocket >= 0 ) {
   		 	processRequest(slaveSocket);
    	}
	}
}

void
processRequest(int fd){
	processTimeRequest(fd);
	close(fd);
}

void
processTimeRequest( int fd )
{
  pthread_mutex_lock(&timemutex);
  struct timeval t;
  gettimeofday(&t,NULL);
  double s=t.tv_sec;
  double ms=t.tv_usec;
  ms = ms/1000000;
  double requestTime=s+ms;
  pthread_mutex_unlock(&timemutex);
    
  // Buffer used to store the name received from the client
  const int MaxName = 1024;
  char name[ MaxName + 1 ];
  int nameLength = 0;
  int n;
  int start_protocol=-1;
  char* docPath=NULL;
  int gotGet=0;
  char method[5];
  char* queryString=NULL;
  
  // Currently character read
  unsigned char newChar;

  // Last character read
  unsigned char lastChar[3]={0};

  //
  // GET <sp> <Document Requested> <sp> HTTP/1.0 <crlf> 
  // Read the name of the client character by character until a
  // <CR><LF> is found.
  //
    
  while ( nameLength < MaxName &&
	  ( n = read( fd, &newChar, sizeof(newChar) ) ) > 0 ) {
	if (newChar==' '){
	//if not seen GET
		if(gotGet==0){
			name[nameLength]=0;
			strcpy(method,name);
			gotGet=1;
		}else if(docPath==NULL){
			name[nameLength]=0;
			docPath=(char*)malloc(sizeof(name));
			strcpy(docPath,name+3);
			printf("docPath:%s\n",docPath);
			start_protocol=0;
		}
    }else if ( lastChar[0]=='\015'&&lastChar[1]=='\012'&&lastChar[2]=='\015' && newChar == '\012' ) {
      // Discard previous <CR> from name
      nameLength-=3;
      break;
    
    }else{
    	if(start_protocol==0){
    		start_protocol=nameLength;
    	}
		name[ nameLength ] = newChar;
		if(nameLength<2){
			for(int i=0; i<=nameLength;i++){
				lastChar[i]=name[i];
			}
		}else{
			for(int i=0;i<3&&nameLength-2+i>=0;i++){
				lastChar[i]=name[nameLength-2+i];
			}
		}
	    nameLength++;
	}
	
  }

  // Add null character at the end of the string
  name[ nameLength ] = 0;
  if(gotGet==0||docPath==NULL)
  	return;
  
  printf( "name=%s\n", name );
  
//   char* ac=strstr(name,"Accept");
//   if(ac==NULL)
//   	acceptH[0]='\0';
//   else{
//  	strcpy(acceptH,ac+7);
//   	ac=strstr(acceptH,"\r\n");
//  	*ac='\0';
//   }
//   
//   ac=strstr(name,"Accept-Encoding");// 
//   if(ac==NULL)
//   	acceptEncoding[0]='\0';
//   else{
//  	strcpy(acceptEncoding,ac+16);
//   	ac=strstr(acceptEncoding,"\r\n");
//  	*ac='\0';
//   }
// 	
// 
//   ac=strstr(name,"Accept-Language");
//   if(ac==NULL)
//   	acceptLanguage[0]='\0';
//   else{
//  	strcpy(acceptLanguage,ac+16);
//   	ac=strstr(acceptLanguage,"\r\n");
//  	*ac='\0';
//   }
// 
//   ac=strstr(name,"Cookie");
//   if(ac==NULL)
//   	cookie[0]='\0';
//   else{
//  	strcpy(cookie,ac+7);
//  	ac=strstr(cookie,"\r\n");
//  	*ac='\0';
//   }
// 
//   ac=strstr(name,"Connection");
//   if(ac==NULL)
//   	connection[0]='\0';
//   else{
//  	strcpy(connection,ac+11);
//   }
// 
//   ac=strstr(name,"Referer");
//   if(ac==NULL)
//   	refer[0]='\0';
//   else{
//  	strcpy(refer,ac+8);
//   	ac=strstr(refer,"\r\n");
//  	*ac='\0';
//   }
// 
//   ac=strstr(name,"User-Agent");
//   if(ac==NULL)
//   	agent[0]='\0';
//   else{
//  	strcpy(agent,ac+11);
//   	ac=strstr(agent,"\r\n");
//  	*ac='\0';
//   }
  char sortBy[3];

  if(strstr(docPath,"?C=N;")){
	sortBy[0]='N';
	if(strstr(docPath,"O=D"))
	 sortBy[1]='D';
	else sortBy[1]='A';
  }else if(strstr(docPath,"?C=M;")){
  	sortBy[0]='M';
	if(strstr(docPath,"O=D"))
	 sortBy[1]='D';
	else sortBy[1]='A';
  }else if(strstr(docPath,"?C=S;")){
  	sortBy[0]='S';
	if(strstr(docPath,"O=D"))
	  sortBy[1]='D';
	else sortBy[1]='A';
  }else if(strstr(docPath,"?C=D;")){
  	sortBy[0]='D';
	if(strstr(docPath,"O=D"))
	 sortBy[1]='D';
	else sortBy[1]='A';
  }else{
  	sortBy[0]='N';
  	sortBy[1]='A';
  }
  sortBy[2]='\0';

  char* dp=strstr(docPath,"?");
  if(dp&&!strncmp(docPath, "/cgi-bin",strlen("/cgi-bin"))){
  	queryString=dp+1;
	queryString[-1]='\0';			
  }else if(dp&&strncmp(dp+4,";O=",3)==0){
  	*dp='\0';
  }
  
  char* protocol=name+start_protocol;
  *(protocol+8)='\0';
   
  char* filePath=(char*)malloc(1024);
  char* cwd=(char*)malloc(256);
  cwd=getcwd(cwd,256);
  strcpy(cwd+strlen(cwd),"/http-root-dir/");
  strcpy(filePath,cwd);

  if(strcmp(docPath,"/")==0){
  	strcpy(filePath+strlen(filePath),"htdocs/index.html");
  }else if(!strcmp(docPath,"/stats")){
  	strcpy(filePath+strlen(filePath),"htdocs/stats.html");
  }else if(!strcmp(docPath,"/logs")){
  	strcpy(filePath+strlen(filePath),"htdocs/logs.html");
  }else{
 	 if(strncmp(docPath,"/icons",strlen("/icons"))!=0&&
 		 strncmp(docPath,"/htdocs",strlen("/htdocs"))!=0&&
 		 strncmp(docPath,"/cgi-bin/",strlen("/cgi-bin"))!=0){
  			strcpy(filePath+strlen(filePath),"htdocs");
 	 }
 	 if(*(filePath+strlen(filePath)-1)=='/'&&*docPath=='/')
 		strcpy(filePath+strlen(filePath),docPath+1);
	 else
	 	strcpy(filePath+strlen(filePath),docPath);
  }

  pthread_mutex_lock(&logmutex);
  FILE * log=fopen("./http-root-dir/htdocs/logs.html","a");
  fwrite("<tr><td valign=\"top\">",1,21,log);
  char sorceHost[1024];
  gethostname(sorceHost, 1023);
  fwrite(sorceHost,1,strlen(sorceHost),log);
  fwrite("</td><td align=\"right\">",1,23,log);
  fwrite(filePath,1,strlen(filePath),log);
  fwrite("</td></tr>\n",1,11,log);
  if(!strcmp(docPath,"/logs")){
  	fwrite("</body>\n</html>\n",1,16,log);
  }
  fclose(log);
  pthread_mutex_unlock(&logmutex);

  
  printf("filepath:%s\n",filePath);
  if(expandPath(filePath,cwd)==NULL){
 	writeNotFound(fd, (char*)"text/html",protocol);
 	pthread_mutex_lock(&timemutex);
 	 	printf("3\n");
	getRequestEndTime(requestTime,docPath);
	 	printf("4\n");
	pthread_mutex_unlock(&timemutex);
  	gotGet=0;
	start_protocol=-1;
	nameLength = 0;
	free(docPath);
	free(cwd);
 	free(filePath);
 	docPath=NULL;
	cwd=NULL;
	filePath=NULL;
	queryString=NULL;
  	return;
  }

  
  char * documentType;
  if(strncmp(filePath+strlen(filePath)-5,".html",sizeof(".html"))==0||
 	strncmp(filePath+strlen(filePath)-6,".html/",sizeof(".html/"))==0){
 		documentType=strdup("text/html\0");
 	}else if(strncmp(filePath+strlen(filePath)-4,".gif",sizeof(".gif"))==0||
 		strncmp(filePath+strlen(filePath)-5,".gif/",sizeof(".gif/"))==0){
 		documentType=strdup("image/gif\0");
 	}else
 		documentType=strdup("text/plain\0");


/* if(strncmp(documentType,"image/gif",9)==0){
 	FILE* readfile=fopen(filePath,"rb");
 	writeFile(fd,readfile,documentType,protocol);
  	fclose(readfile);
 	pthread_mutex_lock(&timemutex);
	getRequestEndTime(requestTime,docPath);
	pthread_mutex_unlock(&timemutex);
  	gotGet=0;
	start_protocol=-1;
	free(docPath);
	free(cwd);
 	free(filePath);
 	docPath=NULL;
	cwd=NULL;
	filePath=NULL;
	return;
 }	*/
 
  struct stat buf;
  struct stat subbuf;

  if(lstat(filePath, &buf)<0){
  	writeNotFound(fd,documentType,protocol);
	pthread_mutex_lock(&timemutex);
	getRequestEndTime(requestTime,docPath);
	pthread_mutex_unlock(&timemutex);
	gotGet=0;
	start_protocol=-1;
	nameLength = 0;
	free(docPath);
	free(cwd);
 	free(filePath);
 	docPath=NULL;
	cwd=NULL;
	filePath=NULL;
	queryString=NULL;
	return;
  }

  if(S_ISDIR(buf.st_mode)){
	int fileLack=0;
	if(*(filePath+strlen(filePath)-1)!='/'){
		fileLack=1;
		strcpy(filePath+strlen(filePath),"/\0");
	}
	
  	int num_of_file=0;
  	Rec* pmyrec=(Rec*)malloc(sizeof(Rec));
  	pmyrec->pre=NULL;
	Rec* p=pmyrec;
  	struct dirent * dirp;
  	DIR * dp=opendir(filePath);

  	while((dirp=readdir(dp))!=NULL){
  		if(strncmp(dirp->d_name,".",1)==0||strncmp(dirp->d_name,"..",2)==0)
  			continue;
  		char* filename=(char*)malloc(strlen(filePath)+strlen(dirp->d_name)+1);
  		strcpy(filename,filePath);
  		strcpy(filename+strlen(filePath),dirp->d_name);
  		*(filename+strlen(filePath)+strlen(dirp->d_name))='\0';
  		if(lstat(filename, &subbuf)<0)
  			return;
  		if(S_ISDIR(subbuf.st_mode)){
  			strcpy(p->kind,"Directory");
  			p->kind[10]='\0';
  		}
  		else{
  			strcpy(p->kind,"File");
  			p->kind[5]='\0';
  		}
  		strcpy(p->name,dirp->d_name);
  		if(fileLack==0||strcmp(p->kind,"File")==0){
  			strcpy(p->link,dirp->d_name);
  		}else{
  			*(filePath+strlen(filePath)-1)='\0';
  			char* fp=strrchr(filePath,'/');
  			*(filePath+strlen(filePath))='/';
  			*(filePath+strlen(filePath))='\0';
  			strcpy(p->link,fp);
  			strcpy(p->link+strlen(p->link),dirp->d_name);
  		}
  		if(strcmp(p->kind,"File")==0)
  			p->size=subbuf.st_size;
  		else p->size=0;
  		ctime_r(&subbuf.st_mtime, p->time);
  		*strstr(p->time,"\n")='\0';
  		num_of_file++;
		p->next=(Rec*)malloc(sizeof(Rec));
		Rec* tmp=p;
		p=p->next;
		p->pre=tmp;
  	}
  	closedir(dp);
  	p=pmyrec;
	for(int i=0;i<(num_of_file-1);i++){
		p=p->next;
	}

	p->next=NULL;
	sort(sortBy, &pmyrec, num_of_file);
	int length=1000;
	char* htmlHeader=(char*)malloc(length);
	strcpy(htmlHeader,"HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\nContent-type: Directory\r\n\r\n<!DOCTYPE HTML PUBLIC \"-//W3C//DTD//EN\">\n<html>\n<head>\n<title>Index of ");
  	strcpy(htmlHeader+150,filePath);
  	strcpy(htmlHeader+150+strlen(filePath),"</title>\n</head>\n<body>\n<h1>Index of ");
  	strcpy(htmlHeader+187+strlen(filePath),filePath);
  	strcpy(htmlHeader+187+2*strlen(filePath),"</h1>");
  	*(htmlHeader+192+2*strlen(filePath))='\0';

    const char* htmlHeaderP2standard;
    if(!strcmp(sortBy,"NA"))
		htmlHeaderP2standard="<table><tr><th></th><th><a href=\"?C=N;O=D\">Name</a></th><th><a href=\"?C=M;O=A\">Last modified</a></th><th><a href=\"?C=S;O=A\">Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>"; 	
	else if(!strcmp(sortBy,"MA"))
		htmlHeaderP2standard="<table><tr><th></th><th><a href=\"?C=N;O=A\">Name</a></th><th><a href=\"?C=M;O=D\">Last modified</a></th><th><a href=\"?C=S;O=A\">Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>"; 	
	else if(!strcmp(sortBy,"SA"))
		htmlHeaderP2standard="<table><tr><th></th><th><a href=\"?C=N;O=A\">Name</a></th><th><a href=\"?C=M;O=A\">Last modified</a></th><th><a href=\"?C=S;O=D\">Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>"; 	
	else if(!strcmp(sortBy,"DA"))
		htmlHeaderP2standard="<table><tr><th></th><th><a href=\"?C=N;O=A\">Name</a></th><th><a href=\"?C=M;O=A\">Last modified</a></th><th><a href=\"?C=S;O=A\">Size</a></th><th><a href=\"?C=D;O=D\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>"; 	
	else
		htmlHeaderP2standard="<table><tr><th></th><th><a href=\"?C=N;O=A\">Name</a></th><th><a href=\"?C=M;O=A\">Last modified</a></th><th><a href=\"?C=S;O=A\">Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>"; 	
	
	strcat(htmlHeader,htmlHeaderP2standard);
	writeParentLink(filePath,&htmlHeader,docPath,cwd,length);
	writeDir(pmyrec, &htmlHeader, num_of_file,length);

 	printf("html:\n%s",htmlHeader);
 	
	write(fd,htmlHeader, strlen(htmlHeader));
	
  }else{  
	FILE* readfile=fopen(filePath,"rb");
	
  	if(readfile<0){
  		writeNotFound(fd,documentType,protocol);
  	}else{
  		if(strncmp(docPath, "/cgi-bin",strlen("/cgi-bin"))==0){
  			int argv=0;
			setenv("REQUEST_METHOD",method,1);
			if(queryString){
				argv=1;
				setenv("QUERY_STRING",queryString,1);
				char* plusMark=strstr(queryString,"+");
				while(plusMark){
					*plusMark=' ';
					plusMark=strstr(queryString,"+");
					argv++;
				}
			}else putenv((char*)"QUERY_STRING=");
		
		char * exefile;
		if(strstr(docPath,".so")){
			exefile=(char*)malloc(strlen(filePath));
			sprintf(exefile,".%s",strrchr(filePath,'/'));
			
			exeLoadableFile(exefile,filePath);
			
		    pthread_mutex_lock(&timemutex);
		    getRequestEndTime(requestTime,docPath);
		    pthread_mutex_unlock(&timemutex);

		    gotGet=0;
			start_protocol=-1;
			nameLength = 0;
			free(docPath);
			free(cwd);
		 	free(filePath);
 			docPath=NULL;
			cwd=NULL;
			filePath=NULL;
			queryString=NULL;
			return;
		}
		
		exefile=strstr(filePath,"/http-root-dir")+1;
		exeCgi(fd,queryString,exefile,protocol);
// 		char host[1024];
// 		gethostname(host, 1023);
		int cgi_w[2];
		if(pipe(cgi_w)<0){
			perror("pipe");
		}
		pid_t pid;
		pid=fork();		
		if(pid==0){
			dup2(cgi_w[1],1);
			dup2(cgi_w[1],2);
// 			setenv("DOCUMENT_ROOT",getcwd(NULL,256),1);
// 			setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
// 			setenv("HTTP_ACCEPT",acceptH,1);
// 			setenv("HTTP_ACCEPT_ENCODING",acceptEncoding,1);	
// 			setenv("HTTP_ACCEPT_LANGUAGE",acceptLanguage,1);
// 			setenv("HTTP_CONNECTION",connection,1);
// 			setenv("HTTP_COOKIE",cookie,1);
// 			setenv("HTTP_HOST",host,1);
// 			setenv("HTTP_REFERER",refer,1);
// 			setenv("HTTP_USER_AGENT",agent,1);
// 			setenv("PATH_TRANSLATED", filePath, 1);	
// 			setenv("REMOTE_ADDR", address, 1);
// 			setenv("REMOTE_PORT", portp, 1);
// 			setenv("REMOTE_HOST", hostName, 1);
//          setenv("REQUEST_URI",docPath,1);
//          setenv("SCRIPT_FILENAME", filePath, 1);
//          setenv("SCRIPT_NAME", docPath, 1);
// 			char uri[1024];
// 			strcpy(uri,host);
// 			strcpy(uri+strlen(host),docPath);
// 			*(uri+strlen(host)+strlen(docPath))='\0';
// 			setenv("SCRIPT_URI",uri,1);
// 			setenv("SCRIPT_URL",docPath,1);
// 			setenv("SERVER_NAME", host, 1);
// 			char ps[20];
// 			sprintf(ps,"%d",port);
// 			setenv("SERVER_PORT", ps, 1);
// 			setenv("SERVER_PROTOCOL", protocol, 1);
// 			putenv((char*)"SERVER_SOFTWARE=");
            char* arg[argv+2];
            arg[0]=exefile;
			if(argv>0){
				if(argv==1){
					arg[1]=queryString;
					arg[2]=NULL;
				}
				else{
       	    		arg[1]=strtok(queryString," ");
        	   	 	for(int i=1;i<argv;i++){
            			arg[i+1]=strtok(NULL," ");
           	 		}
           	 		arg[argv+1]=NULL;
           	 	}
           	}else arg[1]=NULL;
            execvp(arg[0],arg);
            perror("execvp");
            exit(1);						
		}
		waitpid(pid,NULL,0);
        close(cgi_w[1]);

        int outputLen=1024;
        char* output=(char*)malloc(outputLen);
        int count;
        int i=0;
		while(count=read(cgi_w[0], output+i, 1)){
			if(i==outputLen-1){
				outputLen=outputLen*2;
				output=(char*)realloc(output,outputLen);
			}
			i++;
		}
		*(output+i)='\0';
		close(cgi_w[0]);
		
		printf("output:%s\n",output);
  		
		writeCgi(fd,output,protocol);
		
 		pthread_mutex_lock(&timemutex);
		getRequestEndTime(requestTime,docPath);
		pthread_mutex_unlock(&timemutex);
		
		unsetenv("REQUEST_METHOD");
		unsetenv("QUERY_STRING");
		nameLength = 0;
			gotGet=0;
			start_protocol=-1;
			free(docPath);
			free(output);
			free(cwd);
 			free(filePath);
 			docPath=NULL;
			cwd=NULL;
			filePath=NULL;
			output=NULL;
			queryString=NULL;
		  	return;  
		}

  		writeFile(fd,readfile,documentType,protocol);
  		fclose(readfile);
  	}
  }
  
  pthread_mutex_lock(&timemutex);
  getRequestEndTime(requestTime,docPath);
  pthread_mutex_unlock(&timemutex);
  gotGet=0;
  nameLength = 0;
  start_protocol=-1;
  sortBy[0]='\0';
  free(docPath);
  free(cwd);
  free(filePath);
  docPath=NULL;
  cwd=NULL;
  filePath=NULL;
  queryString=NULL
}

void
exeLoadableFile(int fd, char* queryString, char* exefile,char* filePath,char* protocol){
	void* lib=inMod(filePath);
	if(lib==NULL){
		lib=dlopen(exefile, RTLD_LAZY);
		addMod(lib,filePath);
	}
			
	if(lib==NULL){
		fprintf( stderr, "dlerror:%s\n", dlerror()); 
		perror( "dlopen"); 
		exit(1); 
	}
			
	httprun httprunmod=(httprun)dlsym(lib, "httprun");
	if(httprunmod==NULL){
		perror("dlsym");
		exit(1);
	}
			
	char* dex=(char*)malloc(200);
	memcpy(dex,protocol,strlen(protocol));
  	memcpy(dex+strlen(protocol)," 200 Document follows\r\nServer: CS 252 lab5\r\n",44);
	*(dex+44+strlen(protocol))='\0';
	write(fd, dex, strlen(dex));
	httprunmod(fd, queryString);
}

void
exeCgi(int fd,char* queryString,char* exefile,char* protocol){
	
}

void 
getRequestEndTime(int requestTime,char* docPath){
  struct timeval te;
  gettimeofday(&te,NULL);
  double se=te.tv_sec;
  double mse=te.tv_usec;
  double endRequest=se+mse/1000000;
  double diff=endRequest-requestTime;
  char h[1024];
  gethostname(h, 1023);
  if(maxTime==0){
  	maxTime=diff;
  	minTime=diff;
  	sprintf(maxFile,"%s:%d%s",h,port,docPath);
  	sprintf(minFile,"%s:%d%s",h,port,docPath);
  	writeStats();
  }else if(maxTime<diff){
  	maxTime=diff;
  	sprintf(maxFile,"%s:%d%s",h,port,docPath);
  	writeStats();
  }else if(diff<minTime){
  	minTime=diff;
  	sprintf(minFile,"%s:%d%s",h,port,docPath);
  	writeStats();
  }
}

void
writeStats(){
	FILE* stats=fopen("./http-root-dir/htdocs/stats.html","w");
	fwrite("<html>\n<body>\n<p>\n<h1>Stats</h1><br>\n",1,37,stats);
	fwrite("Author: Meixian Li<br>\n",1,23,stats);
	fwrite("Server Has being up for ",1,24,stats);
	
	time_t endTime;
	time(&endTime);
	double upTime=difftime(endTime, startTime);

	char second[100];

	sprintf(second,"%.1f",upTime);

	fwrite(second,1,strlen(second),stats);
	fwrite(" seconds", 1, 8, stats);
	fwrite("<br>\n",1,5,stats);
	
	fwrite("Number of Requests: ",1,20,stats);
	char request[10];
	sprintf(request,"%d",requests);
	fwrite(request,1,strlen(request),stats);
	fwrite("<br>\n",1,5,stats);
	
	fwrite("Max Server Time: Spend ",1,23,stats);
	char maxtime[100];
	sprintf(maxtime,"%f",maxTime);
	fwrite(maxtime,1,strlen(maxtime),stats);
	if(maxFile){
		fwrite(" secs on ",1,9,stats);
		fwrite(maxFile,1,strlen(maxFile),stats);
		fwrite(" request<br>\n",1,13,stats);
	}else fwrite(" secs<br>\n",1,10,stats);
	
	fwrite("Min Server Time: Spend ",1,23,stats);
	char mintime[100];
	sprintf(mintime,"%f",minTime);
	fwrite(mintime,1,strlen(mintime),stats);
	if(minFile){
		fwrite(" secs on ",1,9,stats);
		fwrite(minFile,1,strlen(minFile),stats);
		fwrite(" request<br></body>\n</html>\n",1,28,stats);
	}else fwrite(" secs<br></body>\n</html>\n",1,25,stats);
	
	fclose(stats);
}

char*
expandPath(char* filePath,char* cwd){
  char* expandedFilepath=filePath;
  char* s=strstr(expandedFilepath,"..");
  if(s){
  	char* sp=s-1;
  	char* after;
  	if(*sp!='/'){
  		perror("Invalid");
  		exit(1);
  	}else{
  		while(s!=NULL){
  			sp=s-2;
  			after=s+2;
  			if(after==NULL)
  				break;
  			while(sp&&*sp!='/'){ sp--;};
  			*sp='\0';
  			expandedFilepath=strcat(expandedFilepath,after);
  			s=strstr(expandedFilepath,"..");
  		}
  		if((*(expandedFilepath+strlen(expandedFilepath)-1)!='/'&&strlen(expandedFilepath)<(strlen(cwd)-1))
  		||(*(expandedFilepath+strlen(expandedFilepath)-1)=='/'&&strlen(expandedFilepath)<strlen(cwd))){
  			return NULL;
  		}
  	}
  }
  return expandedFilepath;

}

void
writeCgi(int fd, char* buf,char* protocol){
	char* dex=(char*)malloc(200+strlen(buf));
	memcpy(dex,protocol,strlen(protocol));
  	memcpy(dex+strlen(protocol)," 200 Document follows\r\nServer: CS 252 lab5\r\n",44);
  	memcpy(dex+strlen(protocol)+44,buf,strlen(buf));
	*(dex+44+strlen(protocol)+strlen(buf))='\0';
	write(fd,dex,strlen(dex));
}

void
writeParentLink(char* filePath,char** htmlHeader,char* docPath,char* cwd,int length){
	char* parentPath=(char*)malloc(2*strlen(filePath));

	if(*(filePath+strlen(filePath)-1)!='/'){
		char* p=strrchr(filePath,'/')+1;
		*(filePath+strlen(filePath)-1)='/';
		char pp=*p;
		*p='\0';
		strncpy(parentPath,filePath,strlen(filePath));
		*(parentPath+strlen(filePath))='\0';
		*p=pp; 
	}else{
		*(filePath+strlen(filePath)-1)='\0';
		char* p=strrchr(filePath,'/')+1;
		*(filePath+strlen(filePath)-1)='/';
		char pp=*p;
		*p='\0';
		strncpy(parentPath,filePath,strlen(filePath)); 
		*(parentPath+strlen(filePath))='\0';
		*p=pp;
	}

	char *r=expandPath(parentPath,cwd);

	if(r==NULL){
		return;
	}else if(strcmp(parentPath,cwd)==0){
		strcat(parentPath,"htdocs/index.html");
	}else if(strncmp(parentPath, cwd, strlen(parentPath))==0){
		strcat(parentPath,"/htdocs/index.html");
	}

	parentPath=parentPath+strlen(cwd);
	char* parentLink=(char*)malloc(500);
	strcpy(parentLink, "<tr><td valign=\"top\"><img src=\"/icons/menu.gif\" alt=\"[DIR]\"></td><td><a href=\"/../");
	strcpy(parentLink+82,parentPath);
	strcpy(parentLink+82+strlen(parentPath),"\">Parent Directory</a>       </td><td>&nbsp;</td><td align=\"right\">  - </td></tr>");
	*(parentLink+163+strlen(parentPath))='\0';
	if(strlen(parentLink)>(length-strlen(*htmlHeader))){
		*htmlHeader=(char*)realloc(*htmlHeader,length+strlen(parentLink));
		length=length+strlen(parentLink);
	}

	strcpy(*htmlHeader+strlen(*htmlHeader),parentLink);
}

void
writeDir(Rec* pmyrec, char** htmlHeader, int num,int length){
	
	Rec* rp=pmyrec;
	for(int i=0; rp &&i<num;i++){
		char* fileInfo=(char*)malloc(10000);
		if(strcmp(rp->kind,"Directory")==0){
			strcpy(fileInfo,"<tr><td valign=\"top\"><img src=\"/icons/menu.gif\" alt=\"[DIR]\"></td><td><a href=\"");
			strcpy(fileInfo+78,rp->link);
			strcpy(fileInfo+78+strlen(rp->link),"/\">");
			strcpy(fileInfo+81+strlen(rp->link),rp->name);
			strcpy(fileInfo+81+strlen(rp->link)+strlen(rp->name),"</a>               </td><td align=\"right\">");
			strcpy(fileInfo+123+strlen(rp->link)+strlen(rp->name), rp->time);
			strcpy(fileInfo+123+strlen(rp->link)+strlen(rp->name)+strlen(rp->time), "</td><td align=\"right\">  - </td></tr>");
			*(fileInfo+160+strlen(rp->link)+strlen(rp->name)+strlen(rp->time))='\0';
		}else if(strstr(rp->name,".gif")==NULL){
			strcpy(fileInfo,"<tr><td valign=\"top\"><img src=\"/icons/unknown.gif\" alt=\"[   ]\"></td><td><a href=\"");
			strcpy(fileInfo+81,rp->name);
			strcpy(fileInfo+81+strlen(rp->link),"\">");
		 	strcpy(fileInfo+83+strlen(rp->link), rp->name);
			strcpy(fileInfo+83+strlen(rp->link)+strlen(rp->name),"</a>                      </td><td align=\"right\">");
			strcpy(fileInfo+132+strlen(rp->link)+strlen(rp->name), rp->time);
			strcpy(fileInfo+132+strlen(rp->link)+strlen(rp->name)+strlen(rp->time), "</td><td align=\"right\">  ");
			char s[10];
			sprintf(s, "%jd", rp->size);
			*(s+strlen(s))='\0';
			strcpy(fileInfo+157+strlen(rp->link)+strlen(rp->name)+strlen(rp->time),s);
			strcpy(fileInfo+157+strlen(rp->link)+strlen(rp->name)+strlen(rp->time)+strlen(s), "B </td></tr>");
			*(fileInfo+169+strlen(rp->link)+strlen(rp->name)+strlen(rp->time)+strlen(s))='\0';
		}else{
			strcpy(fileInfo,"<tr><td valign=\"top\"><img src=\"/icons/image.gif\" alt=\"[IMG]\"></td><td><a href=\"");
			strcpy(fileInfo+79,rp->name);
			strcpy(fileInfo+79+strlen(rp->link),"\">");
		 	strcpy(fileInfo+81+strlen(rp->link), rp->name);
			strcpy(fileInfo+81+strlen(rp->link)+strlen(rp->name),"</a>                      </td><td align=\"right\">");
			strcpy(fileInfo+130+strlen(rp->link)+strlen(rp->name), rp->time);
			strcpy(fileInfo+130+strlen(rp->link)+strlen(rp->name)+strlen(rp->time), "</td><td align=\"right\">  ");
			char s[10];
			sprintf(s, "%jd", rp->size);
			*(s+strlen(s))='\0';
			strcpy(fileInfo+155+strlen(rp->link)+strlen(rp->name)+strlen(rp->time),s);
			strcpy(fileInfo+155+strlen(rp->link)+strlen(rp->name)+strlen(rp->time)+strlen(s), "B </td></tr>");
			*(fileInfo+167+strlen(rp->link)+strlen(rp->name)+strlen(rp->time)+strlen(s))='\0';
		}		
		if(strlen(fileInfo)>(length-strlen(*htmlHeader))){
			*htmlHeader=(char*)realloc(*htmlHeader,length+strlen(fileInfo));
			length=length+strlen(fileInfo);			
		}
		strcpy(*htmlHeader+strlen(*htmlHeader),fileInfo);
		free(fileInfo);
		rp=rp->next;
	}
	strcat(*htmlHeader,"<tr><th colspan=\"5\"><hr></th></tr></table></body></html>");

}

void
writeFile(int fd, FILE* readfile,char* documentType,char* protocol){

	char* dex;
	unsigned long lSize;
	fseek (readfile , 0L , SEEK_END);
	lSize = ftell (readfile);
  	rewind(readfile);
	char*  buffer = (char*) malloc (sizeof(char)*lSize);
	fread (buffer,1,lSize,readfile);	
	
	if(lSize>(1000-(strlen(protocol)+strlen(documentType)+120))){
		dex=(char*)malloc((1000+lSize)*sizeof(char));

	} 	else{
  		dex=(char*)malloc(1000*sizeof(char));
  	}
  	memcpy(dex,protocol,strlen(protocol));
  	memcpy(dex+strlen(protocol)," 200 Document follows\r\nServer: CS 252 lab5\r\nContent-type: ",58);
  	memcpy(dex+strlen(protocol)+58, documentType, strlen(documentType));
  	if(strcmp(documentType,"image/gif")==0){	
  		memcpy(dex+strlen(protocol)+strlen(documentType)+58, "Content-Transfer-Encoding: binary\r\nContent-Length: ", 51);
  		char* s=(char*)calloc(16,0);
  		sprintf(s,"%lu",lSize);
  		memcpy(dex+strlen(protocol)+strlen(documentType)+109,s,strlen(s));
  		memcpy(dex+strlen(protocol)+strlen(documentType)+strlen(s)+109,"\r\n\r\n",4);
  		memcpy(dex+strlen(protocol)+strlen(documentType)+strlen(s)+113,buffer,lSize);
		write(fd,dex,strlen(protocol)+strlen(documentType)+strlen(s)+113+lSize);
 	}else{
  		memcpy(dex+strlen(protocol)+strlen(documentType)+58,"\r\n\r\n",4);
  		memcpy(dex+strlen(protocol)+strlen(documentType)+62,buffer,lSize);
		*(dex+strlen(protocol)+strlen(documentType)+62+lSize)='\0';
		write(fd,dex,strlen(dex));
	}
  	free(dex);
	free (buffer);
	buffer=NULL;
  	dex=NULL;
}

void
writeNotFound(int fd, char* documentType,char* protocol){
	char * notFound=(char*)malloc(1000);
	memcpy(notFound,protocol,strlen(protocol));
	memcpy(notFound+strlen(protocol)," 404 File Not Found\r\nServer: CS 252 lab5\r\nContent-type: ",56);
	memcpy(notFound+strlen(protocol)+56,documentType,strlen(documentType));
	memcpy(notFound+strlen(protocol)+56+strlen(documentType),"\r\n\r\n",4);
	memcpy(notFound+strlen(protocol)+60+strlen(documentType),"File Not Found",14);
	*(notFound+strlen(protocol)+74+strlen(documentType))='\0';
	write(fd,notFound,strlen(notFound));
	free(notFound);
}
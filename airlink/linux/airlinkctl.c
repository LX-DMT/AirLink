#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#define SOCKET_PATH "/run/airlinkd.sock"
static void usage(const char *name){fprintf(stderr,"Usage: %s status|mode|wifi provision|wifi cancel|wifi forget|ch347 get|diag export\n",name);}
int main(int argc,char **argv){
 struct sockaddr_un address; char command[128]=""; char reply[4096]; int fd; ssize_t n;
 if(argc<2||argc>3){usage(argv[0]);return 2;}
 for(int i=1;i<argc;++i){if(i>1)strncat(command," ",sizeof(command)-strlen(command)-1U);strncat(command,argv[i],sizeof(command)-strlen(command)-1U);}
 strncat(command,"\n",sizeof(command)-strlen(command)-1U);
 fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
 if(fd<0){fprintf(stderr,"{\"ok\":false,\"error\":\"socket:%s\"}\n",strerror(errno));return 1;}
 memset(&address,0,sizeof(address));address.sun_family=AF_UNIX;snprintf(address.sun_path,sizeof(address.sun_path),"%s",SOCKET_PATH);
 if(connect(fd,(struct sockaddr *)&address,sizeof(address))!=0){fprintf(stderr,"{\"ok\":false,\"error\":\"connect:%s\"}\n",strerror(errno));close(fd);return 1;}
 if(write(fd,command,strlen(command))<0){close(fd);return 1;}
 n=read(fd,reply,sizeof(reply)-1U);close(fd);if(n<=0)return 1;reply[n]='\0';fputs(reply,stdout);return strstr(reply,"\"ok\":true")?0:1;
}

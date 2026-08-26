#define _GNU_SOURCE
#include "airlink_provision.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define WLAN "wlan0"
#define RUND "/run/airlink"
#define DATAD "/data/airlink"
#define APCONF DATAD "/ap.conf"
#define HCONF RUND "/hostapd.conf"
#define DCONF RUND "/dnsmasq.conf"
#define SCANOUT RUND "/scan.txt"
#define APIP "192.168.4.1"
#define MANUAL_MS 300000ULL
#define CLIENT_MS 5000ULL
#include "airlink_portal.inc"
static uint64_t ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return(uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void wipe(void*p,size_t n){volatile unsigned char*q=p;while(n--)*q++=0;}
static void copy_text(char*d,size_t z,const char*s){size_t n;if(!z)return;n=strnlen(s,z-1);memcpy(d,s,n);d[n]=0;}
static int dirs(void)
{
    if (mkdir(RUND, 0755) != 0 && errno != EEXIST)
        return -1;
    if (mkdir(DATAD, 0700) != 0 && errno != EEXIST)
        return -1;
    errno = 0;
    return 0;
}
static int allwrite(int fd,const void*p,size_t n){const char*q=p;while(n){ssize_t r=write(fd,q,n);if(r<0){if(errno==EINTR)continue;return-1;}q+=r;n-=r;}return 0;}
static int atom(const char*path,const char*data,mode_t mode){char tmp[256],dir[256],*s;int fd,d;snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",path,(long)getpid());fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,mode);if(fd<0)return-1;if(allwrite(fd,data,strlen(data))||fsync(fd)){close(fd);unlink(tmp);return-1;}fchmod(fd,mode);close(fd);if(rename(tmp,path)){unlink(tmp);return-1;}snprintf(dir,sizeof(dir),"%s",path);s=strrchr(dir,'/');if(s){*s=0;d=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(d>=0){fsync(d);close(d);}}return 0;}
static int cmd(const char*const a[],char*out,size_t z,unsigned timeout){int p[2],st=127;pid_t pid;size_t u=0;uint64_t end=ms()+timeout;if(z)out[0]=0;if(pipe2(p,O_CLOEXEC|O_NONBLOCK))return 127;pid=fork();if(pid<0){close(p[0]);close(p[1]);return 127;}if(!pid){int n=open("/dev/null",O_WRONLY);dup2(p[1],1);if(n>=0)dup2(n,2);close(p[0]);close(p[1]);execv(a[0],(char*const*)a);_exit(127);}close(p[1]);for(;;){char b[512];ssize_t n=read(p[0],b,sizeof(b));if(n>0&&z>1){size_t c=n;if(c>z-1-u)c=z-1-u;memcpy(out+u,b,c);u+=c;out[u]=0;}if(waitpid(pid,&st,WNOHANG)==pid)break;if(ms()>=end){kill(pid,SIGKILL);waitpid(pid,&st,0);st=127<<8;break;}usleep(10000);}close(p[0]);return WIFEXITED(st)?WEXITSTATUS(st):127;}
static int quiet(const char*const a[],unsigned t){char x[1];return cmd(a,x,sizeof(x),t);}
static pid_t spawn(const char*path,char*const a[]){pid_t p=fork();if(!p){int f=open("/tmp/airlinkd.log",O_WRONLY|O_CREAT|O_APPEND,0644);setsid();if(f>=0){dup2(f,1);dup2(f,2);}execv(path,a);_exit(127);}return p;}
static void killp(pid_t*p){int st;uint64_t e;if(*p<=0)return;kill(*p,SIGTERM);e=ms()+1000;while(ms()<e){if(waitpid(*p,&st,WNOHANG)==*p){*p=0;return;}usleep(20000);}kill(*p,SIGKILL);waitpid(*p,&st,0);*p=0;}
#define FIXED_AP_PASSWORD "12345678"

static bool ap_credentials_match(const char *ssid, const char *password,
                                 const char *expected_ssid)
{
    return ssid && password && expected_ssid &&
           strcmp(ssid, expected_ssid) == 0 &&
           strcmp(password, FIXED_AP_PASSWORD) == 0;
}

static int creds(struct airlink_provision_ctx *c)
{
    FILE *file;
    unsigned mac[6] = {0};
    char mac_text[64];
    char stored_ssid[sizeof(c->ap_ssid)] = {0};
    char stored_password[sizeof(c->ap_password)] = {0};
    char line[96];
    char text[128];

    file = fopen("/sys/class/net/" WLAN "/address", "r");
    if (file) {
        if (fgets(mac_text, sizeof(mac_text), file))
            (void)sscanf(mac_text, "%x:%x:%x:%x:%x:%x",
                         &mac[0], &mac[1], &mac[2],
                         &mac[3], &mac[4], &mac[5]);
        fclose(file);
    }
    snprintf(c->ap_ssid, sizeof(c->ap_ssid), "AirLink-%02X%02X",
             mac[4] & 255U, mac[5] & 255U);
    copy_text(c->ap_password, sizeof(c->ap_password), FIXED_AP_PASSWORD);

    file = fopen(APCONF, "r");
    if (file) {
        while (fgets(line, sizeof(line), file)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "ssid=", 5) &&
                strlen(line + 5) < sizeof(stored_ssid))
                copy_text(stored_ssid, sizeof(stored_ssid), line + 5);
            else if (!strncmp(line, "password=", 9) &&
                     strlen(line + 9) < sizeof(stored_password))
                copy_text(stored_password, sizeof(stored_password), line + 9);
        }
        fclose(file);
    }

    if (ap_credentials_match(stored_ssid, stored_password, c->ap_ssid) &&
        chmod(APCONF, 0600) == 0) {
        wipe(stored_password, sizeof(stored_password));
        return 0;
    }

    snprintf(text, sizeof(text), "ssid=%s\npassword=%s\n",
             c->ap_ssid, c->ap_password);
    if (atom(APCONF, text, 0600) != 0) {
        wipe(text, sizeof(text));
        wipe(stored_password, sizeof(stored_password));
        return -1;
    }
    wipe(text, sizeof(text));
    wipe(stored_password, sizeof(stored_password));
    return 0;
}
static void addnet(struct airlink_provision_ctx *c,
                   struct airlink_scan_network *network)
{
    if (!network->ssid[0])
        return;
    if (!network->security_known)
        network->secured = 1U;
    for (unsigned i = 0; i < c->network_count; ++i) {
        struct airlink_scan_network *stored = &c->networks[i];
        uint32_t secured;
        uint32_t security_known;

        if (strcmp(stored->ssid, network->ssid) != 0)
            continue;
        secured = stored->secured || network->secured;
        security_known = stored->security_known || network->security_known;
        if (network->rssi_dbm > stored->rssi_dbm)
            *stored = *network;
        stored->secured = secured;
        stored->security_known = security_known;
        return;
    }
    if (c->network_count < AIRLINK_PROVISION_MAX_NETWORKS)
        c->networks[c->network_count++] = *network;
}
static int cmpnet(const void*a,const void*b){return((const struct airlink_scan_network*)b)->rssi_dbm-((const struct airlink_scan_network*)a)->rssi_dbm;}

static void scan_parse_security(struct airlink_scan_network *network,
                                const char *line)
{
    if (!strncmp(line, "capability:", 11)) {
        network->security_known = 1U;
        if (strstr(line + 11, "Privacy") != NULL)
            network->secured = 1U;
    } else if (!strncmp(line, "RSN:", 4) ||
               !strncmp(line, "WPA:", 4) ||
               !strncmp(line, "WEP:", 4)) {
        network->security_known = 1U;
        network->secured = 1U;
    }
}

static void scan_parse(struct airlink_provision_ctx *c)
{
    FILE *file = fopen(SCANOUT, "r");
    char line[512];
    struct airlink_scan_network network;
    bool have_bss = false;

    c->network_count = 0;
    memset(&network, 0, sizeof(network));
    network.rssi_dbm = -127;
    if (!file)
        return;
    while (fgets(line, sizeof(line), file)) {
        char *cursor = line;
        while (isspace((unsigned char)*cursor))
            ++cursor;
        cursor[strcspn(cursor, "\r\n")] = 0;
        if (!strncmp(cursor, "BSS ", 4)) {
            if (have_bss)
                addnet(c, &network);
            memset(&network, 0, sizeof(network));
            network.rssi_dbm = -127;
            have_bss = true;
        } else if (!strncmp(cursor, "freq:", 5)) {
            network.frequency_mhz =
                (uint32_t)strtoul(cursor + 5, 0, 10);
        } else if (!strncmp(cursor, "signal:", 7)) {
            network.rssi_dbm =
                (int32_t)strtol(cursor + 7, 0, 10);
        } else if (!strncmp(cursor, "SSID:", 5)) {
            cursor += 5;
            while (isspace((unsigned char)*cursor))
                ++cursor;
            if (strlen(cursor) <= 32U)
                copy_text(network.ssid, sizeof(network.ssid), cursor);
        } else {
            scan_parse_security(&network, cursor);
        }
    }
    fclose(file);
    if (have_bss)
        addnet(c, &network);
    qsort(c->networks, c->network_count, sizeof(c->networks[0]), cmpnet);
}

static int scan_start(struct airlink_provision_ctx *c, uint64_t now)
{
    const char *const up[] = {
        "/sbin/ip", "link", "set", WLAN, "up", 0
    };
    pid_t pid;

    c->network_count = 0;
    unlink(SCANOUT);
    if (quiet(up, 2000) != 0)
        return -1;
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        int output = open(SCANOUT, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        int nullfd = open("/dev/null", O_WRONLY);
        char *argv[] = {"/usr/sbin/iw", "dev", WLAN, "scan", 0};
        if (output < 0)
            _exit(127);
        dup2(output, STDOUT_FILENO);
        if (nullfd >= 0)
            dup2(nullfd, STDERR_FILENO);
        close(output);
        if (nullfd >= 0)
            close(nullfd);
        execv(argv[0], argv);
        _exit(127);
    }
    c->scan_pid = pid;
    c->scan_deadline_ms = now + 8000ULL;
    return 0;
}

static void scan_stop(struct airlink_provision_ctx *c)
{
    killp(&c->scan_pid);
    c->scan_deadline_ms = 0;
    unlink(SCANOUT);
}

static void closehttp(struct airlink_provision_ctx*c){if(c->listen_fd>=0)close(c->listen_fd);c->listen_fd=-1;for(unsigned i=0;i<AIRLINK_PROVISION_MAX_CLIENTS;i++){if(c->clients[i].fd>=0)close(c->clients[i].fd);c->clients[i].fd=-1;c->clients[i].used=0;}}
static void stopap(struct airlink_provision_ctx*c){const char*const fl[]={"/sbin/ip","addr","flush","dev",WLAN,0};scan_stop(c);closehttp(c);killp(&c->dnsmasq_pid);killp(&c->hostapd_pid);quiet(fl,2000);}
static int runtime(struct airlink_provision_ctx*c){char h[768],d[768];snprintf(h,sizeof(h),"interface=" WLAN "\nctrl_interface=/run/hostapd\nssid=%s\nhw_mode=g\nchannel=1\nbeacon_int=100\ndtim_period=2\nmax_num_sta=8\nauth_algs=1\nwpa=2\nwpa_pairwise=CCMP\nrsn_pairwise=CCMP\nwpa_passphrase=%s\nieee80211n=1\nwmm_enabled=1\n",c->ap_ssid,c->ap_password);snprintf(d,sizeof(d),"interface=" WLAN "\nbind-interfaces\nlisten-address=" APIP "\ndhcp-range=192.168.4.20,192.168.4.100,255.255.255.0,10m\ndhcp-option=3," APIP "\ndhcp-option=6," APIP "\naddress=/#/" APIP "\nno-resolv\nno-hosts\ndhcp-authoritative\n");int r=atom(HCONF,h,0600)||atom(DCONF,d,0600);wipe(h,sizeof(h));return r?-1:0;}
static int httpstart(struct airlink_provision_ctx*c){struct sockaddr_in a;int y=1;c->listen_fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(c->listen_fd<0)return-1;setsockopt(c->listen_fd,SOL_SOCKET,SO_REUSEADDR,&y,sizeof(y));memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons(80);inet_aton(APIP,&a.sin_addr);if(bind(c->listen_fd,(void*)&a,sizeof(a))||listen(c->listen_fd,8))return-1;fcntl(c->listen_fd,F_SETFL,fcntl(c->listen_fd,F_GETFL)|O_NONBLOCK);return 0;}
static int startap(struct airlink_provision_ctx*c){const char*const up[]={"/sbin/ip","link","set",WLAN,"up",0},*const fl[]={"/sbin/ip","addr","flush","dev",WLAN,0},*const ad[]={"/sbin/ip","addr","add",APIP"/24","dev",WLAN,0};char*ha[]={"/usr/sbin/hostapd",HCONF,0},*da[]={"/usr/sbin/dnsmasq","--keep-in-foreground","--conf-file=" DCONF,0};int st;if(runtime(c)||quiet(up,2000))return-1;quiet(fl,2000);if(quiet(ad,2000))return-1;c->hostapd_pid=spawn(ha[0],ha);usleep(250000);if(c->hostapd_pid<=0||waitpid(c->hostapd_pid,&st,WNOHANG)==c->hostapd_pid)return-1;c->dnsmasq_pid=spawn(da[0],da);usleep(150000);if(c->dnsmasq_pid<=0||waitpid(c->dnsmasq_pid,&st,WNOHANG)==c->dnsmasq_pid)return-1;return httpstart(c);}
static void esc(char*d,size_t z,const char*s){size_t u=0;for(;*s&&u+2<z;s++){unsigned char x=*s;if(x=='"'||x=='\\')d[u++]='\\';if(x>=32)d[u++]=x;}d[u]=0;}
static void reply(int fd,int code,const char*type,const char*body,const char*extra){char h[512];int n=snprintf(h,sizeof(h),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n%s\r\n",code,code==200?"OK":code==302?"Found":"Bad Request",type,strlen(body),extra?extra:"");send(fd,h,n,MSG_NOSIGNAL);send(fd,body,strlen(body),MSG_NOSIGNAL);}
static int hx(int c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return-1;}
static int dec(char*d,size_t z,const char*s,size_t n){size_t u=0;for(size_t i=0;i<n;i++){unsigned char x=s[i];if(x=='+')x=' ';else if(x=='%'){int a,b;if(i+2>=n||(a=hx(s[i+1]))<0||(b=hx(s[i+2]))<0)return-1;x=(a<<4)|b;i+=2;}if(!x||u+1>=z)return-1;d[u++]=x;}d[u]=0;return 0;}
static int field(const char*b,const char*k,char*d,size_t z){size_t n=strlen(k);for(const char*p=b;*p;){const char*e=strchr(p,'&');size_t l=e?(size_t)(e-p):strlen(p);if(l>n&&!memcmp(p,k,n)&&p[n]=='=')return dec(d,z,p+n+1,l-n-1);if(!e)break;p=e+1;}d[0]=0;return-1;}
static bool valid(const char*s,size_t a,size_t b){size_t n=strlen(s);if(n<a||n>b)return 0;for(;*s;s++)if((unsigned char)*s<32||(unsigned char)*s==127)return 0;return 1;}
static bool hex64(const char*s){if(strlen(s)!=64)return 0;for(;*s;s++)if(!isxdigit((unsigned char)*s))return 0;return 1;}
static int quoted(FILE*f,const char*k,const char*v){fprintf(f,"    %s=\"",k);for(;*v;v++){if(*v=='\\'||*v=='"')fputc('\\',f);fputc(*v,f);}return fputs("\"\n",f)<0?-1:0;}
static int candidate(const char*s,const char*p,bool open){char t[256];FILE*f;snprintf(t,sizeof(t),AIRLINK_PROVISION_CANDIDATE_CONF".tmp.%ld",(long)getpid());f=fopen(t,"w");if(!f)return-1;chmod(t,0600);fputs("ctrl_interface=/run/wpa_supplicant\nupdate_config=0\nnetwork={\n",f);quoted(f,"ssid",s);if(open)fputs("    key_mgmt=NONE\n",f);else if(hex64(p))fprintf(f,"    psk=%s\n    key_mgmt=WPA-PSK\n",p);else{quoted(f,"psk",p);fputs("    key_mgmt=WPA-PSK\n",f);}fputs("}\n",f);fflush(f);if(fsync(fileno(f))){fclose(f);unlink(t);return-1;}fclose(f);if(rename(t,AIRLINK_PROVISION_CANDIDATE_CONF)){unlink(t);return-1;}return 0;}
static int clen(const char*h){const char*p=strcasestr(h,"Content-Length:");if(!p)return 0;p+=15;while(*p==' '||*p=='\t')p++;long n=strtol(p,0,10);return n<0||n>(long)AIRLINK_PROVISION_BODY_MAX?-1:(int)n;}
static void netjson(struct airlink_provision_ctx*c,char*out,size_t z){size_t u=snprintf(out,z,"{\"networks\":[");for(unsigned i=0;i<c->network_count&&u+128<z;i++){char s[96];esc(s,sizeof(s),c->networks[i].ssid);u+=snprintf(out+u,z-u,"%s{\"ssid\":\"%s\",\"rssi_dbm\":%d,\"frequency_mhz\":%u,\"secured\":%s}",i?",":"",s,c->networks[i].rssi_dbm,c->networks[i].frequency_mhz,c->networks[i].secured?"true":"false");}snprintf(out+u,z-u,"]}");}
static void submit(struct airlink_provision_ctx*c,int fd,char*b){char si[24]="",s[128]="",p[128]="",o[8]="";field(b,"session_id",si,sizeof(si));field(b,"ssid",s,sizeof(s));field(b,"password",p,sizeof(p));field(b,"open",o,sizeof(o));bool op=!strcmp(o,"1");const char*e=0;if(strtoul(si,0,10)!=c->session_id)e="stale-session";else if(!valid(s,1,32))e="invalid-ssid";else if(!op&&!(valid(p,8,63)||hex64(p)))e="invalid-password";else if(candidate(s,p,op))e="candidate-write";if(e){c->error=AIRLINK_PROVISION_ERROR_INVALID_INPUT;char j[128];snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"%s\"}",e);reply(fd,400,"application/json",j,0);}else{copy_text(c->target_ssid,sizeof(c->target_ssid),s);c->submit_count++;c->submission_pending=1;c->phase=AIRLINK_PROVISION_SUBMITTED;c->error=0;reply(fd,200,"application/json","{\"ok\":true}",0);}wipe(p,sizeof(p));}
static void request(struct airlink_provision_ctx *c, int fd, char *r)
{
    char method[8];
    char path[256];
    char *header_end = strstr(r, "\r\n\r\n");
    int body_len;

    if (!header_end ||
        sscanf(r, "%7s %255s", method, path) != 2) {
        reply(fd, 400, "text/plain", "bad request", 0);
        return;
    }
    body_len = clen(r);
    if (!strcmp(method, "GET") &&
        (!strcmp(path, "/") || !strcmp(path, "/index.html"))) {
        reply(fd, 200, "text/html; charset=utf-8", airlink_portal_html, 0);
    } else if (!strcmp(method, "GET") &&
               !strcmp(path, "/api/networks")) {
        char json[4096];
        netjson(c, json, sizeof(json));
        reply(fd, 200, "application/json", json, 0);
    } else if (!strcmp(method, "GET") &&
               !strcmp(path, "/api/status")) {
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"ok\":true,\"session_id\":%u,\"phase\":%u,"
                 "\"error\":%u,\"mandatory\":%s}",
                 c->session_id, c->phase, c->error,
                 c->mandatory ? "true" : "false");
        reply(fd, 200, "application/json", json, 0);
    } else if (!strcmp(method, "POST") &&
               !strcmp(path, "/api/provision") && body_len >= 0) {
        char *body = header_end + 4;
        body[body_len] = 0;
        submit(c, fd, body);
        wipe(body, (size_t)body_len);
    } else if (!strcmp(method, "POST") &&
               !strcmp(path, "/api/cancel")) {
        if (c->mandatory) {
            reply(fd, 400, "application/json",
                  "{\"ok\":false,\"error\":\"mandatory\"}", 0);
        } else {
            c->cancel_pending = 1;
            reply(fd, 200, "application/json", "{\"ok\":true}", 0);
        }
    } else {
        reply(fd, 302, "text/plain", "",
              "Location: http://" APIP "/\r\n");
    }
}
static void servicehttp(struct airlink_provision_ctx *c, uint64_t now)
{
    if (c->listen_fd >= 0) {
        for (;;) {
            int fd = accept4(c->listen_fd, 0, 0,
                             SOCK_CLOEXEC | SOCK_NONBLOCK);
            unsigned slot;
            if (fd < 0)
                break;
            for (slot = 0; slot < AIRLINK_PROVISION_MAX_CLIENTS; ++slot)
                if (c->clients[slot].fd < 0)
                    break;
            if (slot == AIRLINK_PROVISION_MAX_CLIENTS) {
                close(fd);
                continue;
            }
            c->clients[slot].fd = fd;
            c->clients[slot].used = 0;
            c->clients[slot].deadline_ms = now + CLIENT_MS;
        }
    }

    for (unsigned i = 0; i < AIRLINK_PROVISION_MAX_CLIENTS; ++i) {
        struct airlink_http_client *client = &c->clients[i];
        bool close_client = false;

        if (client->fd < 0)
            continue;
        if (now >= client->deadline_ms) {
            close_client = true;
        } else {
            size_t available = sizeof(client->request) - 1U - client->used;
            ssize_t count;
            if (available == 0U) {
                reply(client->fd, 400, "text/plain", "too large", 0);
                close_client = true;
            } else {
                count = recv(client->fd, client->request + client->used,
                             available, 0);
                if (count > 0) {
                    char *header_end;
                    int body_len;
                    size_t header_size;
                    size_t total_size;
                    client->used += (uint32_t)count;
                    client->request[client->used] = 0;
                    header_end = strstr(client->request, "\r\n\r\n");
                    if (!header_end) {
                        if (client->used >= AIRLINK_PROVISION_HEADER_MAX) {
                            reply(client->fd, 400, "text/plain",
                                  "header too large", 0);
                            close_client = true;
                        }
                    } else {
                        header_size = (size_t)(header_end -
                                              client->request) + 4U;
                        body_len = clen(client->request);
                        if (header_size > AIRLINK_PROVISION_HEADER_MAX ||
                            body_len < 0) {
                            reply(client->fd, 400, "text/plain",
                                  "request too large", 0);
                            close_client = true;
                        } else {
                            total_size = header_size + (size_t)body_len;
                            if (client->used >= total_size) {
                                request(c, client->fd, client->request);
                                close_client = true;
                            }
                        }
                    }
                } else if (count == 0) {
                    close_client = true;
                } else if (errno != EAGAIN && errno != EWOULDBLOCK &&
                           errno != EINTR) {
                    close_client = true;
                }
            }
        }
        if (close_client) {
            wipe(client->request, sizeof(client->request));
            close(client->fd);
            client->fd = -1;
            client->used = 0;
        }
    }
}
const char*airlink_provision_phase_name(uint32_t p){static const char*n[]={"IDLE","SCANNING","AP_STARTING","AP_READY","SUBMITTED","STA_TESTING","SUCCESS","FAILED","CANCELLING"};return p<9?n[p]:"UNKNOWN";}
int airlink_provision_init(struct airlink_provision_ctx *c)
{
    memset(c, 0, sizeof(*c));
    c->listen_fd = -1;
    c->scan_pid = 0;
    for (unsigned i = 0; i < AIRLINK_PROVISION_MAX_CLIENTS; i++)
        c->clients[i].fd = -1;
    if (dirs() || creds(c))
        return -1;
    c->phase = AIRLINK_PROVISION_IDLE;
    errno = 0;
    return 0;
}
int airlink_provision_begin(struct airlink_provision_ctx*c,bool mandatory,bool saved,uint64_t now){stopap(c);c->active=1;c->mandatory=mandatory;c->has_saved_config=saved;c->submission_pending=c->cancel_pending=c->timeout_pending=0;c->started_ms=now;c->manual_deadline_ms=mandatory?0:now+MANUAL_MS;if(++c->session_id==0)c->session_id=1;c->target_ssid[0]=0;c->error=0;c->phase=AIRLINK_PROVISION_SCANNING;c->scan_deadline_ms=0;return 0;}
void airlink_provision_service(struct airlink_provision_ctx*c,uint64_t now)
{
    int status;
    pid_t result;

    if (c->active && c->phase == AIRLINK_PROVISION_SCANNING) {
        if (c->scan_pid <= 0) {
            if (scan_start(c, now) != 0) {
                c->error = AIRLINK_PROVISION_ERROR_SCAN;
                c->phase = AIRLINK_PROVISION_AP_STARTING;
            }
        } else {
            result = waitpid(c->scan_pid, &status, WNOHANG);
            if (result == c->scan_pid) {
                c->scan_pid = 0;
                c->scan_deadline_ms = 0;
                scan_parse(c);
                unlink(SCANOUT);
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                    c->error = AIRLINK_PROVISION_ERROR_SCAN;
                c->phase = AIRLINK_PROVISION_AP_STARTING;
            } else if (result < 0 && errno == ECHILD) {
                c->scan_pid = 0;
                c->scan_deadline_ms = 0;
                scan_parse(c);
                unlink(SCANOUT);
                c->error = AIRLINK_PROVISION_ERROR_SCAN;
                c->phase = AIRLINK_PROVISION_AP_STARTING;
            } else if (c->scan_deadline_ms && now >= c->scan_deadline_ms) {
                killp(&c->scan_pid);
                c->scan_deadline_ms = 0;
                scan_parse(c);
                unlink(SCANOUT);
                c->error = AIRLINK_PROVISION_ERROR_SCAN;
                c->phase = AIRLINK_PROVISION_AP_STARTING;
            }
        }
    } else if (c->active && c->phase == AIRLINK_PROVISION_AP_STARTING) {
        if (startap(c) != 0) {
            stopap(c);
            c->phase = AIRLINK_PROVISION_FAILED;
            c->error = AIRLINK_PROVISION_ERROR_AP_START;
        } else {
            c->phase = AIRLINK_PROVISION_AP_READY;
        }
    }

    if (c->active && c->listen_fd >= 0 &&
        (c->hostapd_pid <= 0 || c->dnsmasq_pid <= 0 ||
         kill(c->hostapd_pid, 0) != 0 ||
         kill(c->dnsmasq_pid, 0) != 0)) {
        stopap(c);
        c->phase = AIRLINK_PROVISION_FAILED;
        c->error = AIRLINK_PROVISION_ERROR_AP_START;
    }
    servicehttp(c, now);
    if (c->active && !c->mandatory && c->manual_deadline_ms &&
        now >= c->manual_deadline_ms &&
        (c->phase == AIRLINK_PROVISION_AP_READY ||
         c->phase == AIRLINK_PROVISION_FAILED)) {
        c->timeout_pending = 1;
        c->manual_deadline_ms = 0;
    }
}
void airlink_provision_stop(struct airlink_provision_ctx*c){stopap(c);unlink(AIRLINK_PROVISION_CANDIDATE_CONF);c->active=0;c->phase=AIRLINK_PROVISION_IDLE;c->error=0;c->submission_pending=c->cancel_pending=c->timeout_pending=0;c->target_ssid[0]=0;}
int airlink_provision_restart_ap(struct airlink_provision_ctx*c,uint32_t e,uint64_t now){int rc;stopap(c);unlink(AIRLINK_PROVISION_CANDIDATE_CONF);c->active=1;c->phase=AIRLINK_PROVISION_FAILED;c->error=e;c->submission_pending=0;c->manual_deadline_ms=c->mandatory?0:now+MANUAL_MS;rc=startap(c);if(rc){stopap(c);c->error=AIRLINK_PROVISION_ERROR_AP_START;}return rc;}
int airlink_provision_take_submission(struct airlink_provision_ctx*c,char*s,uint32_t z){if(!c->submission_pending)return 0;c->submission_pending=0;copy_text(s,z,c->target_ssid);return 1;}
bool airlink_provision_take_cancel(struct airlink_provision_ctx*c){bool v=c->cancel_pending;c->cancel_pending=0;return v;}
bool airlink_provision_take_timeout(struct airlink_provision_ctx*c){bool v=c->timeout_pending;c->timeout_pending=0;return v;}
void airlink_provision_mark_sta_testing(struct airlink_provision_ctx*c){stopap(c);c->phase=AIRLINK_PROVISION_STA_TESTING;c->error=0;}
void airlink_provision_mark_success(struct airlink_provision_ctx*c){unlink(AIRLINK_PROVISION_CANDIDATE_CONF);c->active=0;c->phase=AIRLINK_PROVISION_SUCCESS;c->error=0;}
void airlink_provision_fill_status(const struct airlink_provision_ctx*c,struct airlink_ipc_provision_status*s,uint64_t now){memset(s,0,sizeof(*s));s->owner=AIRLINK_IPC_OWNER_LINUX;if(c->active)s->flags|=AIRLINK_PROVISION_FLAG_ACTIVE;if(c->listen_fd>=0)s->flags|=AIRLINK_PROVISION_FLAG_AP_READY;if(c->mandatory)s->flags|=AIRLINK_PROVISION_FLAG_MANDATORY;if(c->has_saved_config)s->flags|=AIRLINK_PROVISION_FLAG_HAS_SAVED_CONFIG;if(c->phase==AIRLINK_PROVISION_SUBMITTED||c->phase==AIRLINK_PROVISION_STA_TESTING)s->flags|=AIRLINK_PROVISION_FLAG_SUBMITTED;if(c->phase==AIRLINK_PROVISION_SUCCESS)s->flags|=AIRLINK_PROVISION_FLAG_SUCCESS;if(c->phase==AIRLINK_PROVISION_FAILED)s->flags|=AIRLINK_PROVISION_FLAG_FAILED;s->phase=c->phase;s->error=c->error;s->session_id=c->session_id;inet_aton(APIP,(struct in_addr*)&s->ap_ipv4);copy_text(s->ap_ssid,sizeof(s->ap_ssid),c->ap_ssid);copy_text(s->ap_password,sizeof(s->ap_password),c->ap_password);copy_text(s->target_ssid,sizeof(s->target_ssid),c->target_ssid);s->submit_count=c->submit_count;if(c->started_ms&&now>=c->started_ms)s->elapsed_sec=(now-c->started_ms)/1000;}


int airlink_provision_selftest(void)
{
    char decoded[64];
    char field_value[64];
    char ascii32[33];
    char ascii33[34];
    char hex_password[65];
    struct airlink_scan_network security;

    memset(ascii32, 'A', sizeof(ascii32) - 1U);
    ascii32[sizeof(ascii32) - 1U] = 0;
    memset(ascii33, 'B', sizeof(ascii33) - 1U);
    ascii33[sizeof(ascii33) - 1U] = 0;
    memset(hex_password, 'a', sizeof(hex_password) - 1U);
    hex_password[sizeof(hex_password) - 1U] = 0;

    if (dec(decoded, sizeof(decoded), "%E4%B8%AD%E6%96%87+Wi-Fi", 24U) != 0 ||
        strcmp(decoded, "\xe4\xb8\xad\xe6\x96\x87 Wi-Fi") != 0)
        return 1;
    if (dec(decoded, sizeof(decoded), "bad%2", 5U) == 0 ||
        dec(decoded, sizeof(decoded), "nul%00x", 7U) == 0)
        return 2;
    if (field("ssid=A%26B%2BC&open=0", "ssid",
              field_value, sizeof(field_value)) != 0 ||
        strcmp(field_value, "A&B+C") != 0)
        return 3;
    if (!valid(ascii32, 1U, 32U) || valid(ascii33, 1U, 32U))
        return 4;
    if (!valid("testpass", 8U, 63U) || valid("short7", 8U, 63U) ||
        !hex64(hex_password))
        return 5;
    if (ap_credentials_match("", "", "AirLink-1234") ||
        ap_credentials_match("AirLink-1234", "RANDOMPASS", "AirLink-1234") ||
        ap_credentials_match("AirLink-1234", "87654321", "AirLink-1234") ||
        ap_credentials_match("AirLink-FFFF", FIXED_AP_PASSWORD,
                             "AirLink-1234") ||
        !ap_credentials_match("AirLink-1234", FIXED_AP_PASSWORD,
                              "AirLink-1234"))
        return 6;
    memset(&security, 0, sizeof(security));
    scan_parse_security(&security,
                        "capability: ESS Privacy ShortSlotTime (0x0411)");
    if (!security.security_known || !security.secured)
        return 7;
    memset(&security, 0, sizeof(security));
    scan_parse_security(&security,
                        "capability: ESS ShortSlotTime (0x0401)");
    if (!security.security_known || security.secured)
        return 8;
    memset(&security, 0, sizeof(security));
    scan_parse_security(&security, "RSN:");
    if (!security.security_known || !security.secured)
        return 9;
    memset(&security, 0, sizeof(security));
    copy_text(security.ssid, sizeof(security.ssid), "UnknownSecurity");
    security.rssi_dbm = -50;
    {
        struct airlink_provision_ctx scan_context;
        memset(&scan_context, 0, sizeof(scan_context));
        addnet(&scan_context, &security);
        if (scan_context.network_count != 1U ||
            !scan_context.networks[0].secured)
            return 10;
    }
    return 0;
}

#include <fstream>
#include <string>
#include <vector>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include "ping.h"

using std::string;
using std::vector;
using std::ifstream;

#define PACKET_SIZE         4096
#define MAX_WAIT_TIME       1
#define MAX_PACKETS_COUNT   4
#define MAX_PING_INTERVAL   300
#define PING_DEST_HOST1     "114.114.114.114"
#define PING_DEST_HOST2     "8.8.8.8"

struct IcmpEchoReply {
    int icmpSeq;
    int icmpLen;
    int ipTtl;
    double rtt;
    std::string fromAddr;
    bool isReply;
};

struct PingResult {
    int dataLen;
    int nsend;
    int nreceived;
    char ip[32];
    std::string error;
    std::vector<IcmpEchoReply> icmpEchoReplys;
};

//network status
enum InternetConnectivity {
    UNAVAILABLE = 0,
    AVAILABLE,
    UNKNOW
};

static pthread_mutex_t m_ping_lock = PTHREAD_MUTEX_INITIALIZER;
static char m_sendpacket[PACKET_SIZE];
static char m_recvpacket[PACKET_SIZE];
static int m_maxPacketSize = MAX_PACKETS_COUNT;
static int m_sockfd;
static int m_datalen = 56;
static int m_nsend = 0;
static int m_nreceived = 0;
static int m_icmp_seq = 0;
static struct sockaddr_in m_dest_addr;
static struct sockaddr_in m_from_addr;
static pid_t m_pid;
int m_ping_interval = 1;
static int m_network_status = 0;
static bool m_pinging = false;

static unsigned short getChksum(unsigned short *addr,int len) {
    int nleft = len;
    int sum = 0;
    unsigned short *w = addr;
    unsigned short answer = 0;

    while (nleft > 1) {
        sum += *w++;
        nleft-= 2;
    }

    if (nleft == 1) {
        *(unsigned char *)(&answer) = *(unsigned char *)w;
        sum += answer;
    }

    sum = ((sum>>16) + (sum&0xffff));
    sum += (sum>>16);
    answer = ~sum;

    return answer;
}

static int packIcmp(int pack_no, struct icmp* icmp) {
    int packsize;
    struct icmp *picmp;
    struct timeval *tval;

    picmp = icmp;
    picmp->icmp_type = ICMP_ECHO;
    picmp->icmp_code = 0;
    picmp->icmp_cksum = 0;
    picmp->icmp_seq = pack_no;
    picmp->icmp_id = m_pid;
    packsize = (8 + m_datalen);
    tval= (struct timeval *)icmp->icmp_data;
    gettimeofday(tval, nullptr);
    picmp->icmp_cksum = getChksum((unsigned short *)icmp, packsize);

    return packsize;
}

static bool sendPacket() {
    size_t packetsize;
    while( m_nsend < m_maxPacketSize) {
        m_nsend ++;
        m_icmp_seq ++;
        packetsize = packIcmp(m_icmp_seq, (struct icmp*)m_sendpacket);

        if (sendto(m_sockfd,m_sendpacket, packetsize, 0, (struct sockaddr *) &m_dest_addr, sizeof(m_dest_addr)) < 0) {
            //printf("Ping sendto failed:%s.", strerror(errno));
            continue;
        }
    }

    return true;
}

static struct timeval timevalSub(struct timeval timeval1, struct timeval timeval2) {
    struct timeval result;

    result = timeval1;

    if ((result.tv_usec < timeval2.tv_usec) && (timeval2.tv_usec < 0)) {
        -- result.tv_sec;
        result.tv_usec += 1000000;
    }

    result.tv_sec -= timeval2.tv_sec;

    return result;
}

static bool unpackIcmp(char *buf, int len, struct IcmpEchoReply *icmpEchoReply) {
    int iphdrlen;
    struct ip *ip;
    struct icmp *icmp;
    struct timeval *tvsend, tvrecv, tvresult;
    double rtt;

    ip = (struct ip *)buf;
    iphdrlen = ip->ip_hl << 2;
    icmp = (struct icmp *)(buf + iphdrlen);
    len -= iphdrlen;

    if (len < 8) {
        printf("ICMP packets's length is less than 8.");
        return false;
    }

    if( (icmp->icmp_type == ICMP_ECHOREPLY) && (icmp->icmp_id == m_pid) ) {
        tvsend = (struct timeval *)icmp->icmp_data;
        gettimeofday(&tvrecv, nullptr);
        tvresult = timevalSub(tvrecv, *tvsend);
        rtt = tvresult.tv_sec*1000 + tvresult.tv_usec/1000;  //ms
        icmpEchoReply->rtt = rtt;
        icmpEchoReply->icmpSeq = icmp->icmp_seq;
        icmpEchoReply->ipTtl = ip->ip_ttl;
        icmpEchoReply->icmpLen = len;

        return true;
    } else {
        return false;
    }
}

static bool recvPacket(PingResult &pingResult) {
    int len = 0;
    struct IcmpEchoReply icmpEchoReply;
    int maxfds = m_sockfd + 1;
    int nfd  = 0;
    fd_set rset;
    struct timeval timeout;
    socklen_t fromlen = sizeof(m_from_addr);

    timeout.tv_sec = MAX_WAIT_TIME;
    timeout.tv_usec = 0;

    FD_ZERO(&rset);

    for (int recvCount = 0; recvCount < m_maxPacketSize; recvCount ++) {
        FD_SET(m_sockfd, &rset);
        if ((nfd = select(maxfds, &rset, nullptr, nullptr, &timeout)) == -1) {
            printf("Ping recv select failed:%s.", strerror(errno));
            continue;
        }

        if (nfd == 0) {
            icmpEchoReply.isReply = false;
            pingResult.icmpEchoReplys.push_back(icmpEchoReply);
            continue;
        }

        if (FD_ISSET(m_sockfd, &rset)) {
            if ((len = recvfrom(m_sockfd,
                                m_recvpacket,
                                sizeof(m_recvpacket),
                                0,
                                (struct sockaddr *)&m_from_addr,
                                &fromlen)) <0) {
                if(errno == EINTR) {
                    continue;
                }
                printf("Ping recvfrom failed: %s.", strerror(errno));
                continue;
            }

            icmpEchoReply.fromAddr = inet_ntoa(m_from_addr.sin_addr) ;
            if (strncmp(icmpEchoReply.fromAddr.c_str(), pingResult.ip, strlen(pingResult.ip)) != 0) {
                recvCount--;
                continue;
            }
        }

        if (!unpackIcmp(m_recvpacket, len, &icmpEchoReply)) {
            recvCount--;
            continue;
        }

        icmpEchoReply.isReply = true;
        pingResult.icmpEchoReplys.push_back(icmpEchoReply);
        m_nreceived ++;
    }

    return true;
}

static bool getsockaddr(const char * hostOrIp, struct sockaddr_in* sockaddr) {
    struct hostent *host;
    struct sockaddr_in dest_addr;
    unsigned long inaddr = 0l;

    bzero(&dest_addr,sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;

    inaddr = inet_addr(hostOrIp);
    if (inaddr == INADDR_NONE) {
        host = gethostbyname(hostOrIp);
        if (host == nullptr) {
            return false;
        }
        memcpy( (char *)&dest_addr.sin_addr,host->h_addr, host->h_length);
    } else if (!inet_aton(hostOrIp, &dest_addr.sin_addr)) {
        return false;
    }

    *sockaddr = dest_addr;

    return true;
}

static bool ping(string host, int count, PingResult& pingResult) {
    int size = 50 * 1024;
    IcmpEchoReply icmpEchoReply;

    m_nsend = 0;
    m_nreceived = 0;
    pingResult.icmpEchoReplys.clear();
    m_maxPacketSize = count;
    m_pid = getpid();

    pingResult.dataLen = m_datalen;

    if ((m_sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0) {
        printf("Ping socket failed:%s.", strerror(errno));
        pingResult.error = strerror(errno);
        return false;
    }

    if (setsockopt(m_sockfd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) != 0) {
        printf("Setsockopt SO_RCVBUF failed:%s.", strerror(errno));
        close(m_sockfd);
        return false;
    }

    if (!getsockaddr(host.c_str(), &m_dest_addr)) {
        pingResult.error = "unknow host " + host;
        close(m_sockfd);
        return false;
    }

    strcpy(pingResult.ip, inet_ntoa(m_dest_addr.sin_addr));

    sendPacket();
    recvPacket(pingResult);

    pingResult.nsend = m_nsend;
    pingResult.nreceived = m_nreceived;

    close(m_sockfd);

    return true;
}

bool rk_ping(char *address)
{
    string hostOrIp;
    int nsend = 0, nreceived = 0;
    bool ret;
    PingResult pingResult;
    InternetConnectivity networkResult = UNAVAILABLE;

    pthread_mutex_lock(&m_ping_lock);

    if (address)
        hostOrIp = address;
    else
        hostOrIp = PING_DEST_HOST1;

    printf("%s: ping %s\n", __func__, hostOrIp.c_str());

    for (int count = 1; count <= MAX_PACKETS_COUNT; count ++) {
        memset(&pingResult.ip, 0x0, 32);
        ret = ping(hostOrIp, 1, pingResult);

        if (!ret) {
            printf("Ping error:%s\n", pingResult.error.c_str());
        } else {
            nsend += pingResult.nsend;
            nreceived += pingResult.nreceived;
            if (nreceived > 0)
                break;
        }

        if (count == 2) {
		    if (address)
		        hostOrIp = address;
		    else
		        hostOrIp = PING_DEST_HOST2;
        }
    }

    if (nreceived > 0) {
        ret = true;
        networkResult = AVAILABLE;
        if (m_network_status == (int)UNAVAILABLE) {
            m_ping_interval = 1;
        } else {
            if (m_ping_interval < MAX_PING_INTERVAL) {
                m_ping_interval = m_ping_interval * 2;
                if (m_ping_interval > MAX_PING_INTERVAL) {
                    m_ping_interval = MAX_PING_INTERVAL;
                }
            }
        }
        m_network_status = 1;
    } else {
        ret = false;
        networkResult = UNAVAILABLE;
        m_network_status = 0;
        m_ping_interval = 1;
    }

    pthread_mutex_unlock(&m_ping_lock);

    return ret;
}

/*
 * some code borrowed from sniffex.c
 *
 * Version 0.1.1 (2005-07-05)
 * Copyright (c) 2005 The Tcpdump Group
 *
 ****************************************************************************
 *
 * Below is an excerpt from an email from Guy Harris on the tcpdump-workers
 * mail list when someone asked, "How do I get the length of the TCP
 * payload?" Guy Harris' slightly snipped response (edited by him to
 * speak of the IPv4 header length and TCP data offset without referring
 * to bitfield structure members) is reproduced below:
 *
 * The Ethernet size is always 14 bytes.
 *
 * <snip>...</snip>
 *
 * In fact, you *MUST* assume the Ethernet header is 14 bytes, *and*, if
 * you're using structures, you must use structures where the members
 * always have the same size on all platforms, because the sizes of the
 * fields in Ethernet - and IP, and TCP, and... - headers are defined by
 * the protocol specification, not by the way a particular platform's C
 * compiler works.)
 *
 * The IP header size, in bytes, is the value of the IP header length,
 * as extracted from the "ip_vhl" field of "struct sniff_ip" with
 * the "IP_HL()" macro, times 4 ("times 4" because it's in units of
 * 4-byte words).  If that value is less than 20 - i.e., if the value
 * extracted with "IP_HL()" is less than 5 - you have a malformed
 * IP datagram.
 *
 * The TCP header size, in bytes, is the value of the TCP data offset,
 * as extracted from the "th_offx2" field of "struct sniff_tcp" with
 * the "TH_OFF()" macro, times 4 (for the same reason - 4-byte words).
 * If that value is less than 20 - i.e., if the value extracted with
 * "TH_OFF()" is less than 5 - you have a malformed TCP segment.
 *
 * So, to find the IP header in an Ethernet packet, look 14 bytes after
 * the beginning of the packet data.  To find the TCP header, look
 * "IP_HL(ip)*4" bytes after the beginning of the IP header.  To find the
 * TCP payload, look "TH_OFF(tcp)*4" bytes after the beginning of the TCP
 * header.
 *
 * To find out how much payload there is:
 *
 * Take the IP *total* length field - "ip_len" in "struct sniff_ip"
 * - and, first, check whether it's less than "IP_HL(ip)*4" (after
 * you've checked whether "IP_HL(ip)" is >= 5).  If it is, you have
 * a malformed IP datagram.
 *
 * Otherwise, subtract "IP_HL(ip)*4" from it; that gives you the length
 * of the TCP segment, including the TCP header.  If that's less than
 * "TH_OFF(tcp)*4" (after you've checked whether "TH_OFF(tcp)" is >= 5),
 * you have a malformed TCP segment.
 *
 * Otherwise, subtract "TH_OFF(tcp)*4" from it; that gives you the
 * length of the TCP payload.
 *
 * Note that you also need to make sure that you don't go past the end
 * of the captured data in the packet - you might, for example, have a
 * 15-byte Ethernet packet that claims to contain an IP datagram, but if
 * it's 15 bytes, it has only one byte of Ethernet payload, which is too
 * small for an IP header.  The length of the captured data is given in
 * the "caplen" field in the "struct pcap_pkthdr"; it might be less than
 * the length of the packet, if you're capturing with a snapshot length
 * other than a value >= the maximum packet size.
 * <end of response>
 *
 ****************************************************************************
 *
 * Example compiler command-line for GCC:
 *   gcc -Wall -o sniffex sniffex.c -lpcap
 *
 ****************************************************************************
 *
 * Code Comments
 *
 * This section contains additional information and explanations regarding
 * comments in the source code. It serves as documentaion and rationale
 * for why the code is written as it is without hindering readability, as it
 * might if it were placed along with the actual code inline. References in
 * the code appear as footnote notation (e.g. [1]).
 *
 * 1. Ethernet headers are always exactly 14 bytes, so we define this
 * explicitly with "#define". Since some compilers might pad structures to a
 * multiple of 4 bytes - some versions of GCC for ARM may do this -
 * "sizeof (struct sniff_ethernet)" isn't used.
 *
 * 2. Check the link-layer type of the device that's being opened to make
 * sure it's Ethernet, since that's all we handle in this example. Other
 * link-layer types may have different length headers (see [1]).
 *
 * 3. This is the filter expression that tells libpcap which packets we're
 * interested in (i.e. which packets to capture). Since this source example
 * focuses on IP and TCP, we use the expression "ip", so we know we'll only
 * encounter IP packets. The capture filter syntax, along with some
 * examples, is documented in the tcpdump man page under "expression."
 * Below are a few simple examples:
 *
 * Expression                   Description
 * ----------                   -----------
 * ip                                   Capture all IP packets.
 * tcp                                  Capture only TCP packets.
 * tcp port 80                  Capture only TCP packets with a port equal to 80.
 * ip host 10.1.2.3             Capture all IP packets to or from host 10.1.2.3.
 *
 ****************************************************************************
 *
 */

#define APP_NAME                "hbtad"
#define APP_DESC                "Implementation of hbtad"
#define APP_COPYRIGHT   "Copyright (c) 2011 Jordan Wilberding"
#define APP_DISCLAIMER  "THERE IS ABSOLUTELY NO WARRANTY FOR THIS PROGRAM."

//#define HAVE_REMOTE

#include <pcap.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* default snap length (maximum bytes per packet to capture) */
#define SNAP_LEN 1518

/* ethernet headers are always exactly 14 bytes [1] */
#define SIZE_ETHERNET 14

/* Ethernet addresses are 6 bytes */
#define ETHER_ADDR_LEN  6

/* Ethernet header */
struct sniff_ethernet {
        u_char  ether_dhost[ETHER_ADDR_LEN];    /* destination host address */
        u_char  ether_shost[ETHER_ADDR_LEN];    /* source host address */
        u_short ether_type;                     /* IP? ARP? RARP? etc */
};

/* IP header */
struct sniff_ip {
        u_char  ip_vhl;                 /* version << 4 | header length >> 2 */
        u_char  ip_tos;                 /* type of service */
        u_short ip_len;                 /* total length */
        u_short ip_id;                  /* identification */
        u_short ip_off;                 /* fragment offset field */
        #define IP_RF 0x8000            /* reserved fragment flag */
        #define IP_DF 0x4000            /* dont fragment flag */
        #define IP_MF 0x2000            /* more fragments flag */
        #define IP_OFFMASK 0x1fff       /* mask for fragmenting bits */
        u_char  ip_ttl;                 /* time to live */
        u_char  ip_p;                   /* protocol */
        u_short ip_sum;                 /* checksum */
        struct  in_addr ip_src,ip_dst;  /* source and dest address */
};
#define IP_HL(ip)               (((ip)->ip_vhl) & 0x0f)
#define IP_V(ip)                (((ip)->ip_vhl) >> 4)

/* TCP header */
typedef u_int tcp_seq;

struct sniff_tcp {
        u_short th_sport;               /* source port */
        u_short th_dport;               /* destination port */
        tcp_seq th_seq;                 /* sequence number */
        tcp_seq th_ack;                 /* acknowledgement number */
        u_char  th_offx2;               /* data offset, rsvd */
#define TH_OFF(th)      (((th)->th_offx2 & 0xf0) >> 4)
        u_char  th_flags;
        #define TH_FIN  0x01
        #define TH_SYN  0x02
        #define TH_RST  0x04
        #define TH_PUSH 0x08
        #define TH_ACK  0x10
        #define TH_URG  0x20
        #define TH_ECE  0x40
        #define TH_CWR  0x80
        #define TH_FLAGS        (TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)
        u_short th_win;                 /* window */
        u_short th_sum;                 /* checksum */
        u_short th_urp;                 /* urgent pointer */
};

// only care about most sig 8 bits
int src_ip_addrs[256];
int dst_ip_addrs[256];

// only care about ports 0-1023
int src_ports[1024];
int dst_ports[1024];

// tcp, udp, icmp, or ip
int protocols[4];

// defined above
int packet_sizes[SNAP_LEN];

// flags 8 flags, 256 combinations
int flags[256];


void
got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet);

void
print_payload(const u_char *payload, int len);

void
print_hex_ascii_line(const u_char *payload, int len, int offset);

void
print_app_banner(void);

void
print_app_usage(void);

float std_dev_mult(int **vectors, int num_vecs, int vec_len);
float std_dev(float *vals, int n);
void mean_vec(int *m_vec, int **vecs, int num_vecs, int vec_len);

int main(int argc, char *argv[])
{
  int i;
  printf("Loading data..\n");
  load(argc, argv);
  //printf("Extracting features..\n");
  for (i = 0; i < 256; i++)
  {
    printf("saddr: %d\t count: %d\n", i, src_ip_addrs[i]);
  }

  for (i = 0; i < 256; i++)
  {
    printf("daddr: %d\t count: %d\n", i, dst_ip_addrs[i]);
  }

  for (i = 0; i < 1024; i++)
  {
    printf("sport: %d\t count: %d\n", i, src_ports[i]);
  }

  for (i = 0; i < 1024; i++)
  {
    printf("dport: %d\t count: %d\n", i, dst_ports[i]);
  }

  for (i = 0; i < 4; i++)
  {
    printf("protocol: %d\t count: %d\n", i, protocols[i]);
  }

  for (i = 0; i < SNAP_LEN; i++)
  {
    printf("packet size: %d\t count: %d\n", i, packet_sizes[i]);
  }

  printf("Mapping to metric space..\n");
  printf("Clustering..\n");
  printf("Classifying..\n");
  printf("Finished.\n");

  return 0;
}

/*
 * app name/banner
 */
void
print_app_banner(void)
{

        printf("%s - %s\n", APP_NAME, APP_DESC);
        printf("%s\n", APP_COPYRIGHT);
        printf("%s\n", APP_DISCLAIMER);
        printf("\n");

return;
}

/*
 * print help text
 */
void
print_app_usage(void)
{

        printf("Usage: %s [file]\n", APP_NAME);
        printf("\n");
        printf("Options:\n");
        printf("    file    Process file that contains pcap dump.\n");
        printf("\n");

return;
}

/*
 * print data in rows of 16 bytes: offset   hex   ascii
 *
 * 00000   47 45 54 20 2f 20 48 54  54 50 2f 31 2e 31 0d 0a   GET / HTTP/1.1..
 */
void
print_hex_ascii_line(const u_char *payload, int len, int offset)
{

        int i;
        int gap;
        const u_char *ch;

        /* offset */
        printf("%05d   ", offset);

        /* hex */
        ch = payload;
        for(i = 0; i < len; i++) {
                printf("%02x ", *ch);
                ch++;
                /* print extra space after 8th byte for visual aid */
                if (i == 7)
                        printf(" ");
        }
        /* print space to handle line less than 8 bytes */
        if (len < 8)
                printf(" ");

        /* fill hex gap with spaces if not full line */
        if (len < 16) {
                gap = 16 - len;
                for (i = 0; i < gap; i++) {
                        printf("   ");
                }
        }
        printf("   ");

        /* ascii (if printable) */
        ch = payload;
        for(i = 0; i < len; i++) {
                if (isprint(*ch))
                        printf("%c", *ch);
                else
                        printf(".");
                ch++;
        }

        printf("\n");

return;
}

/*
 * print packet payload data (avoid printing binary data)
 */
void
print_payload(const u_char *payload, int len)
{

        int len_rem = len;
        int line_width = 16;                    /* number of bytes per line */
        int line_len;
        int offset = 0;                                 /* zero-based offset counter */
        const u_char *ch = payload;

        if (len <= 0)
                return;

        /* data fits on one line */
        if (len <= line_width) {
                print_hex_ascii_line(ch, len, offset);
                return;
        }

        /* data spans multiple lines */
        for ( ;; ) {
                /* compute current line length */
                line_len = line_width % len_rem;
                /* print line */
                print_hex_ascii_line(ch, line_len, offset);
                /* compute total remaining */
                len_rem = len_rem - line_len;
                /* shift pointer to remaining bytes to print */
                ch = ch + line_len;
                /* add offset */
                offset = offset + line_width;
                /* check if we have line width chars or less */
                if (len_rem <= line_width) {
                        /* print last line and get out */
                        print_hex_ascii_line(ch, len_rem, offset);
                        break;
                }
        }

return;
}

/*
 * dissect/print packet
 */
void
got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet)
{

        static int count = 1;                   /* packet counter */

        /* declare pointers to packet headers */
        const struct sniff_ethernet *ethernet;  /* The ethernet header [1] */
        const struct sniff_ip *ip;              /* The IP header */
        const struct sniff_tcp *tcp;            /* The TCP header */
        const char *payload;                    /* Packet payload */

        int size_ip;
        int size_tcp;
        int size_payload;

        //printf("\rPacket number %d:", count);
        printf("\nPacket number %d:\n", count);
        count++;

        /* define ethernet header */
        ethernet = (struct sniff_ethernet*)(packet);

        /* define/compute ip header offset */
        ip = (struct sniff_ip*)(packet + SIZE_ETHERNET);
        size_ip = IP_HL(ip)*4;
        if (size_ip < 20) {
                printf("   * Invalid IP header length: %u bytes\n", size_ip);
                return;
        }

        /* print source and destination IP addresses */
        //printf("       From: %s\n", inet_ntoa(ip->ip_src));
        // we only care about lower 8 bits of the address
        //unsigned long src_ip_addr = ip->ip_src.s_addr;
        //printf("       From: %lu\n", src_ip_addr);
        //printf("       From: %d\n", (int)((src_ip_addr >> 24) & 0xFF));
        src_ip_addrs[(int)((ip->ip_src.s_addr >> 24) & 0xFF)]++;
        //printf("         To: %s\n", inet_ntoa(ip->ip_dst));
        dst_ip_addrs[(int)((ip->ip_dst.s_addr >> 24) & 0xFF)]++;

        /* determine protocol */
        switch(ip->ip_p) {
                case IPPROTO_TCP:
                  //printf("   Protocol: TCP\n");
                  protocols[0]++;
                        break;
                case IPPROTO_UDP:
                  //printf("   Protocol: UDP\n");
                  protocols[1]++;
                  packet_sizes[SIZE_ETHERNET + size_ip]++;
                        return;
                case IPPROTO_ICMP:
                  //printf("   Protocol: ICMP\n");
                  protocols[2]++;
                  packet_sizes[SIZE_ETHERNET + size_ip]++;
                        return;
                case IPPROTO_IP:
                  //printf("   Protocol: IP\n");
                  protocols[3]++;
                  packet_sizes[SIZE_ETHERNET + size_ip]++;
                        return;
                default:
                  //printf("   Protocol: unknown\n");
                  // Consider if we want to keep track of these or not
                  packet_sizes[SIZE_ETHERNET + size_ip]++;
                        return;
        }

        /*
         *  OK, this packet is TCP.
         */

        /* define/compute tcp header offset */
        tcp = (struct sniff_tcp*)(packet + SIZE_ETHERNET + size_ip);
        size_tcp = TH_OFF(tcp)*4;
        if (size_tcp < 20) {
                printf("   * Invalid TCP header length: %u bytes\n", size_tcp);
                return;
        }

        flags[(int)tcp->th_flags]++;

        //printf("   Src port: %d\n", ntohs(tcp->th_sport));
        if (tcp->th_sport < 1024)
          src_ports[tcp->th_sport]++;
        //printf("   Dst port: %d\n", ntohs(tcp->th_dport));
        if (tcp->th_dport < 1024)
          dst_ports[tcp->th_dport]++;

        /* define/compute tcp payload (segment) offset */
        payload = (u_char *)(packet + SIZE_ETHERNET + size_ip + size_tcp);

        /* compute tcp payload (segment) size */
        size_payload = ntohs(ip->ip_len) - (size_ip + size_tcp);
        //printf("Payload size: %d\n", size_payload);

        if (size_payload+SIZE_ETHERNET + size_ip < SNAP_LEN)
          packet_sizes[size_payload]++;
        else
          printf("PACKET OVERSIZED: %d bytes\n", size_payload+SIZE_ETHERNET+size_ip);


        //if (size_payload > max_payload_size)
        //  max_payload_size = size_payload;

        /*
         * Print payload data; it might be binary, so don't just
         * treat it as a string.
         */
        //if (size_payload > 0) {
        //        printf("   Payload (%d bytes):\n", size_payload);
        //        print_payload(payload, size_payload);
        //}

return;
}

int live(int argc, char **argv)
{
    char *dev = NULL;                   /* capture device name */
    char errbuf[PCAP_ERRBUF_SIZE];              /* error buffer */
    pcap_t *handle;                             /* packet capture handle */

    char filter_exp[] = "ip";           /* filter expression [3] */
    struct bpf_program fp;                      /* compiled filter program (expression) */
    bpf_u_int32 mask;                   /* subnet mask */
    bpf_u_int32 net;                    /* ip */
    int num_packets = 10;                       /* number of packets to capture */

    print_app_banner();

    /* check for capture device name on command-line */
    if (argc == 2) {
        dev = argv[1];
    }
    else if (argc > 2) {
        fprintf(stderr, "error: unrecognized command-line options\n\n");
        print_app_usage();
        exit(EXIT_FAILURE);
    }
    else {
        /* find a capture device if not specified on command-line */
        dev = pcap_lookupdev(errbuf);
        if (dev == NULL) {
            fprintf(stderr, "Couldn't find default device: %s\n",
                    errbuf);
            exit(EXIT_FAILURE);
        }
    }

    /* get network number and mask associated with capture device */
    if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) {
        fprintf(stderr, "Couldn't get netmask for device %s: %s\n",
                dev, errbuf);
        net = 0;
        mask = 0;
    }

    /* print capture info */
    printf("Device: %s\n", dev);
    printf("Number of packets: %d\n", num_packets);
    printf("Filter expression: %s\n", filter_exp);

    /* open capture device */
    handle = pcap_open_live(dev, SNAP_LEN, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
        exit(EXIT_FAILURE);
    }

    /* make sure we're capturing on an Ethernet device [2] */
    if (pcap_datalink(handle) != DLT_EN10MB) {
        fprintf(stderr, "%s is not an Ethernet\n", dev);
        exit(EXIT_FAILURE);
    }

    /* compile the filter expression */
    if (pcap_compile(handle, &fp, filter_exp, 0, net) == -1) {
        fprintf(stderr, "Couldn't parse filter %s: %s\n",
                filter_exp, pcap_geterr(handle));
        exit(EXIT_FAILURE);
    }

    /* apply the compiled filter */
    if (pcap_setfilter(handle, &fp) == -1) {
        fprintf(stderr, "Couldn't install filter %s: %s\n",
                filter_exp, pcap_geterr(handle));
        exit(EXIT_FAILURE);
    }

    /* now we can set our callback function */
    pcap_loop(handle, num_packets, got_packet, NULL);

    /* cleanup */
    pcap_freecode(&fp);
    pcap_close(handle);

    printf("\nCapture complete.\n");

    return 0;
}

int load(int argc, char **argv)
{
    char *dev = NULL;                   /* capture device name */
    char errbuf[PCAP_ERRBUF_SIZE];              /* error buffer */
    pcap_t *handle;                             /* packet capture handle */

    char filter_exp[] = "ip";           /* filter expression [3] */
    struct bpf_program fp;                      /* compiled filter program (expression) */
    bpf_u_int32 mask;                   /* subnet mask */
    bpf_u_int32 net;                    /* ip */
    int num_packets = 0;                       /* number of packets to capture */
    int i;

    //int src_ip_addrs[256];

    //print_app_banner();

    /* check for capture device name on command-line */
    if (argc == 2) {
        dev = argv[1];
    }
    else {
        fprintf(stderr, "error: unrecognized command-line options\n\n");
        print_app_usage();
        exit(EXIT_FAILURE);
    }

    // setup data values
    for (i = 0; i < 256; i++)
    {
      src_ip_addrs[i] = 0;
      dst_ip_addrs[i] = 0;
    }

    for (i = 0; i < 1024; i++)
    {
      src_ports[i] = 0;
      dst_ports[i] = 0;
    }

    for (i = 0; i < 4; i++)
    {
      protocols[i] = 0;
    }

    for (i = 0; i < SNAP_LEN; i++)
    {
      packet_sizes[i] = 0;
    }

    for (i = 0; i < 256; i++)
    {
      flags[i] = 0;
    }

    if ((handle = pcap_open_offline(dev, errbuf)) == NULL)
    {
        printf("Unable to open the file");
        return -1;
    }

    /* compile the filter expression */
    if (pcap_compile(handle, &fp, filter_exp, 0, net) == -1) {
        fprintf(stderr, "Couldn't parse filter %s: %s\n",
                filter_exp, pcap_geterr(handle));
        exit(EXIT_FAILURE);
    }

    /* apply the compiled filter */
    if (pcap_setfilter(handle, &fp) == -1) {
        fprintf(stderr, "Couldn't install filter %s: %s\n",
                filter_exp, pcap_geterr(handle));
        exit(EXIT_FAILURE);
    }

    /* now we can set our callback function */
    pcap_loop(handle, num_packets, got_packet, NULL);

    /* cleanup */
    pcap_freecode(&fp);
    pcap_close(handle);

    printf("\nCapture complete.\n");

    return 0;
}

// normalized euclidean distance between two vectors
float n_e_d(int *vec1, int *vec2, int len)
{
  int i;
  float d = 0;
  float sd = 0;
  float vals[2];

  for (i = 0; i < len; i++)
  {
    vals[0] = vec1[i];
    vals[1] = vec2[i];
    sd = std_dev(vals, 2);
    d += sqrt(pow(abs(vec1[i] - vec2[i]), 2)/pow(sd,2));
  }

  return d;
}

/*float std_dev_mult(int **vectors, int num_vecs, int vec_len)
{
  float std_dev;
  int *vec;
  int i,j;
  int sum;

  // first, sum vectors to a single vector
  vec = malloc(vec_len*sizeof(vec));

  for (i = 0; i < vec_len; i++)
  {
    sum = 0;

    for (j = 0; j < num_vecs; j++)
    {
      sum += vectors[j][i];
    }

    vec[i] = sum;
  }

  std_dev = std_dev(vec, vec_len);

  free(vec);

  return std_dev;
  }*/

float std_dev(float *vals, int n)
{
  int i;

  if(n == 0)
    return 0.0;
  double sum = 0;
  for(i = 0; i < n; ++i)
    sum += vals[i];
  float mean = sum / n;
  float sq_diff_sum = 0;
  for(i = 0; i < n; ++i) {
    float diff = vals[i] - mean;
    sq_diff_sum += diff * diff;
  }
  float variance = sq_diff_sum / n;
  return sqrt(variance);
}

// kmeans impl, returns mapping of idx of array to cluster, make sure to free it{
int *kmeans(int **vecs, int num_vecs, int vec_len, int num_clusters)
{
    int *map;   // store mapping of vectors to a cluster
    int i,j;
    int **mean_vecs;  // centroids
    int dist, min_dist, min_centroid;
    int **tmp_vecs;  // place to hold pointers to current vecs that need to compute centroid
    int tmp_vecs_idx; // to keep track of count

    if (num_vecs < num_clusters)
    {
        printf("ERROR! kmeans: num_vecs < num_clusters\n");
        return NULL;
    }

    map = malloc(num_vecs*sizeof(int));
    mean_vecs = malloc(num_clusters*vec_len*sizeof(int));
    tmp_vecs = malloc(num_vecs*sizeof(int*));

    // assume the first n vecs are the initial centroids
    for (i = 0; i < num_clusters; i++)
    {
        for (j = 0; j < vec_len; j++)
        {
            mean_vecs[i][j] = vecs[i][j];
        }
    }

    min_dist = INT_MAX;

    // assign each vector to a cluster by distance
    for (i = 0; i < num_vecs; i++)
    {
        for (j = 0; j < num_clusters; j++)
        {
            dist = n_e_d(vecs[i], mean_vecs[j], vec_len);

            if (dist < min_dist)
            {
                dist = min_dist;
                min_centroid = j;
            }
        }

        map[i] = min_centroid;
    }

    // compute new centroids
    for (i = 0; i < num_clusters; i++)
    {
        tmp_vecs_idx = 0;

        for (j = 0; j < num_vecs; j++)
        {
            if (map[j] == i)
            {
                tmp_vecs[tmp_vecs_idx] = vecs[j];
                tmp_vecs_idx++;
            }
        }

        mean_vec(mean_vecs[i], tmp_vecs, tmp_vecs_idx, vec_len);
    }

  return NULL;
}

// calculate mean vector from a set of vectors, store in m_vec
void mean_vec(int *m_vec, int **vecs, int num_vecs, int vec_len)
{
    //int *m_vec;
  int sum;
  int i,j;

  m_vec = malloc(vec_len*sizeof(int));

  for(i = 0; i < vec_len; i++)
  {
    sum = 0;

    for(j = 0; j < num_vecs; j++)
    {
      sum += vecs[j][i];
    }

    m_vec[i] = sum/num_vecs;
  }
}













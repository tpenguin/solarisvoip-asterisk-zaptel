/*
 * Tool to read snoop packet capture (RFC 1761) and analyse the packets for TDMoE
 * timing and ordering.
 *
 * Usage: snoop-tdmoe [fb mac/remote system mac] [local system mac] < snoop-capture
 *
 * WARNING: It only supports span 0 and 1 presently. As this is what I needed.
 *
 * Written by Joseph Benden <joe@thrallingpenguin.com>
 *
 * Copyright (C) 2006-2007 Thralling Penguin LLC. All rights reserved.
 *
 */

#include <iostream>
#include <vector>
#include <sys/types.h>
#include <sys/byteorder.h>
#include "math.h"

#ifndef min
#define min(x, y)       (((x) < (y)) ? (x) : (y))
#endif
#ifndef max
#define max(x, y)       (((x) > (y)) ? (x) : (y))
#endif

typedef struct snoop2_header {
    char        sh_magic[8];
    uint32_t    sh_version;
    uint32_t    sh_dl_type;
} snoop_header_t;

typedef struct snoop2_packet {
    uint32_t    sp_orig_len;
    uint32_t    sp_incl_len;
    uint32_t    sp_packet_record_len;
    uint32_t    sp_cumulative_drops;
    uint32_t    sp_ts_sec;
    uint32_t    sp_ts_msec;
} snoop_packet_t;

typedef struct eth_header {
    unsigned char eh_src[6];
    unsigned char eh_dst[6];
    unsigned short eh_type;
} eth_header_t;

typedef struct zap_header {
    uint16_t    zh_span;
    uint8_t     zh_chunk_size;
    uint8_t     zh_sflags;
    uint16_t    zh_seq;
    uint16_t    zh_channels;
} zap_header_t;

typedef struct span_acct_debug {
    uint32_t        sad_cnt;
    uint64_t        sad_msec;
    uint32_t        sad_min_msec;
    uint32_t        sad_max_msec;
    uint32_t        sad_inv_order;
    std::vector<uint32_t>    sad_vms;
} span_acct_debug_t;

typedef struct span_debug {
    span_acct_debug_t   sd_sad_fb;
    span_acct_debug_t   sd_sad_zap;
    uint32_t            sd_last_seen;
} span_debug_t;

double
avg(std::vector<uint32_t> &in)
{
    double res = 0.0;

    for (std::vector<uint32_t>::const_iterator i = in.begin(); i != in.end(); i++) {
        res += *i;
    }
    return (res / in.size());
}

double
variance(std::vector<uint32_t> &in)
{
    double m = avg(in);
    double res = 0.0;

    for (std::vector<uint32_t>::const_iterator i = in.begin(); i != in.end(); i++) {
        res += pow((*i - m), 2.0);
    }
    return (res / in.size());
}

double
stddev(std::vector<uint32_t> &in)
{
    return (sqrt(variance(in)));
}

void
dump_zap(zap_header_t *z)
{
    std::cout << "Zaptel Header Information:" << std::endl;
    std::cout << "  Span         : " << ntohs(z->zh_span) << std::endl;
    std::cout << "  Chunk Size   : " << ntohs(z->zh_chunk_size) << std::endl;
    std::cout << "  SFlags       : " << ntohs(z->zh_sflags) << std::endl;
    std::cout << "  Sequence     : " << ntohs(z->zh_seq) << std::endl;
    std::cout << "  # of Channels: " << ntohs(z->zh_channels) << std::endl;
    std::cout << std::endl;
}

void
dump_header(snoop_header_t *h)
{
    std::cout << "Header Record Information:" << std::endl;
    std::cout << "  Magic        : " << h->sh_magic << std::endl;
    std::cout << "  Version      : " << ntohl(h->sh_version) << std::endl;
    std::cout << "  Datalink Type: " << ntohl(h->sh_dl_type) << std::endl;
    std::cout << std::endl;
}

void
dump_packet(snoop_packet_t *p)
{
    std::cout << "Packet Record Information:" << std::endl;
    std::cout << "  Original Length     : " << ntohl(p->sp_orig_len) << std::endl;
    std::cout << "  Included Length     : " << ntohl(p->sp_incl_len) << std::endl;
    std::cout << "  Packet Record Length: " << ntohl(p->sp_packet_record_len) << std::endl;
    std::cout << "  Cumulative Drops    : " << ntohl(p->sp_cumulative_drops) << std::endl;
    std::cout << "  Seconds             : " << ntohl(p->sp_ts_sec) << std::endl;
    std::cout << "  Milliseconds        : " << ntohl(p->sp_ts_msec) << std::endl;
    std::cout << std::endl;
}

void
dump_ether(eth_header_t *e)
{
    std::cout << "Ethernet Frame Information:" << std::endl;
    std::cout << "  Type           : " << std::hex << ntohs(e->eh_type) << std::endl;
    std::cout << std::endl;
}

int
digit2int (char d)
{
  switch (d)
    {
    case 'F':
    case 'E':
    case 'D':
    case 'C':
    case 'B':
    case 'A':
      return d - 'A' + 10;
    case 'f':
    case 'e':
    case 'd':
    case 'c':
    case 'b':
    case 'a':
      return d - 'a' + 10;
    case '9':
    case '8':
    case '7':
    case '6':
    case '5':
    case '4':
    case '3':
    case '2':
    case '1':
    case '0':
      return d - '0';
    }
  return -1;
}

int
hex2int (char *s)
{
  int res;
  int tmp;
  /* Gotta be at least one digit */
  if (strlen (s) < 1)
    return -1;
  /* Can't be more than two */
  if (strlen (s) > 2)
    return -1;
  /* Grab the first digit */
  res = digit2int (s[0]);
  if (res < 0)
    return -1;
  tmp = res;
  /* Grab the next */
  if (strlen (s) > 1)
    {
      res = digit2int (s[1]);
      if (res < 0)
	return -1;
      tmp = tmp * 16 + res;
    }
  return tmp;
}

void
readmac (unsigned char *dst, char *mac)
{
  char *ptr;
  int i, res;

  ptr = strchr (mac, ':');
  for (i = 0; i < 6; i++)
    {
      if (mac)
	{
	  if (ptr)
	    {
	      *ptr = '\0';
	      ptr++;
	    }
	  res = hex2int (mac);
	  if (res < 0)
	    break;

	  dst[i] = res & 0xFF;
	}
      else
	break;
      if ((mac = ptr))
	ptr = strchr (mac, ':');
    }

  if (i != 6)
    {
      std::cerr << "Invalid MAC address specified." << std::endl;
      exit (-1);
    }
  free (ptr);
}

int
main(int argc, char *argv[])
{
    snoop_header_t header;
    unsigned char fb[6];
    unsigned char zap[6];
    snoop_packet_t packet;
    eth_header_t eh;
    zap_header_t zh;
    long to_seek;
    uint64_t t64;
    int fb_span, zap_span;
    span_debug_t spans[2];

    // Remove Sun's contention with kernel syncing calls
    std::cout.sync_with_stdio(false);

    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " [FB MAC] [Zap MAC]" << std::endl;
        return (-1);
    }

    memset(&spans, 0, sizeof(spans));
    spans[0].sd_sad_fb.sad_min_msec = 999999;
    spans[0].sd_sad_zap.sad_min_msec = 999999;
    spans[1].sd_sad_fb.sad_min_msec = 999999;
    spans[1].sd_sad_zap.sad_min_msec = 999999;

    readmac(fb, argv[2]);
    readmac(zap, argv[1]);

    std::cin.read((char *)&header, sizeof(snoop_header_t));
    // dump_header(&header);

    // Validate we've got a proper file format
    if (strncmp("snoop", header.sh_magic, 5) != 0) {
        std::cerr << "Invalid file magic number found." << std::endl;
        goto invalidformat;
    }
    if (2 != ntohl(header.sh_version)) {
        std::cerr << "Invalid Snoop file version found." << std::endl;
        goto invalidformat;
    }
    if (4 != ntohl(header.sh_dl_type)) {
        std::cerr << "Invalid datalink type. We only support Ethernet (type 4.)" << std::endl;
        goto invalidformat;
    }

    // Processing loop
    while (!std::cin.fail() && std::cin.good() && !std::cin.eof()) {
        // read a record
        std::cin.read((char *)&packet, sizeof(snoop_packet_t));
        //dump_packet(&packet);
    
        std::cin.read((char *)&eh, sizeof(eth_header_t));
        //dump_ether(&eh);
        if (eh.eh_type != htons(0xd00d)) {
            std::cerr << "Packet is not a zaptel frame." << std::endl;
        } else {
        
            std::cin.read((char *)&zh, sizeof(zap_header_t));
            //dump_zap(&zh);
    
            uint16_t span = ntohs(zh.zh_span);
    
            if (memcmp(eh.eh_src, fb, 6) == 0) {
                spans[span].sd_sad_fb.sad_cnt++;
                if (spans[span].sd_sad_fb.sad_msec != 0) {
                    t64 = ntohl(packet.sp_ts_msec) + (ntohl(packet.sp_ts_sec) * 1000000);
                    t64 -= spans[span].sd_sad_fb.sad_msec;
                    // std::cout << "Timing is " << t64 << std::endl;
                    if (t64 > 1200 || t64 < 800) {
                        std::cout << "ERROR in timing on span " << span << " from FB: " << t64 << " microseconds" << std::endl;
                    } else {
                        spans[span].sd_sad_fb.sad_min_msec = min(spans[span].sd_sad_fb.sad_min_msec, t64);
                        spans[span].sd_sad_fb.sad_max_msec = max(spans[span].sd_sad_fb.sad_max_msec, t64);
                        spans[span].sd_sad_fb.sad_vms.push_back(t64);
                    }
                }
                spans[span].sd_sad_fb.sad_msec = ntohl(packet.sp_ts_msec) + (ntohl(packet.sp_ts_sec) * 1000000);
    
                // invalid ordering?
                if (spans[span].sd_last_seen == 1) {
                    spans[span].sd_sad_fb.sad_inv_order++;
                }
                spans[span].sd_last_seen = 1;
            } else if (memcmp(eh.eh_src, zap, 6) == 0) {
                spans[span].sd_sad_zap.sad_cnt++;
                if (spans[span].sd_sad_zap.sad_msec != 0) {
                    t64 = ntohl(packet.sp_ts_msec) + (ntohl(packet.sp_ts_sec) * 1000000);
                    t64 -= spans[span].sd_sad_zap.sad_msec;
                    //std::cout << "Timing is " << t64 << std::endl;
                    if (t64 > 1200 || t64 < 800) {
                        std::cout << "ERROR in timing on span " << span << " from Zap: " << t64 << " microseconds" << std::endl;
                    } else {
                        spans[span].sd_sad_zap.sad_min_msec = min(spans[span].sd_sad_zap.sad_min_msec, t64);
                        spans[span].sd_sad_zap.sad_max_msec = max(spans[span].sd_sad_zap.sad_max_msec, t64);
                        spans[span].sd_sad_zap.sad_vms.push_back(t64);
                    }
                }
                spans[span].sd_sad_zap.sad_msec = ntohl(packet.sp_ts_msec) + (ntohl(packet.sp_ts_sec) * 1000000);
    
                // invalid ordering?
                if (spans[span].sd_last_seen == 2) {
                    spans[span].sd_sad_zap.sad_inv_order++;
                }
                spans[span].sd_last_seen = 2;
            }
        }
    
        to_seek = ntohl(packet.sp_packet_record_len) - (sizeof(zap_header_t) + sizeof(eth_header_t) + sizeof(snoop_packet_t));

        while (to_seek > 0) {
            std::cin.get();
            --to_seek;
        }
    }

    for (int span = 0; span < 2; span++) {
        std::cout << "Span #" << span << std::endl;
        std::cout << "  FB Frame Count                    : " << spans[span].sd_sad_fb.sad_cnt << std::endl;
        std::cout << "  Zap Frame Count                   : " << spans[span].sd_sad_zap.sad_cnt << std::endl;
        std::cout << "  FB Framing Minimum timing gap     : " << spans[span].sd_sad_fb.sad_min_msec << " microseconds" << std::endl;
        std::cout << "  FB Framing Maximum timing gap     : " << spans[span].sd_sad_fb.sad_max_msec << " microseconds" << std::endl;
        std::cout << "  FB Framing Average timing gap     : " << avg(spans[span].sd_sad_fb.sad_vms) << " microseconds" << std::endl;
        std::cout << "  FB Framing StdDev timing gap      : " << stddev(spans[span].sd_sad_fb.sad_vms) << " microseconds" << std::endl;
        std::cout << "  Zap Framing Minimum timing gap    : " << spans[span].sd_sad_zap.sad_min_msec << " microseconds" << std::endl;
        std::cout << "  Zap Framing Maximum timing gap    : " << spans[span].sd_sad_zap.sad_max_msec << " microseconds" << std::endl;
        std::cout << "  Zap Framing Average timing gap    : " << avg(spans[span].sd_sad_zap.sad_vms) << " microseconds" << std::endl;
        std::cout << "  Zap Framing StdDev timing gap     : " << stddev(spans[span].sd_sad_zap.sad_vms) << " microseconds" << std::endl;
        std::cout << "  FB Framing invalid ordering count : " << spans[span].sd_sad_fb.sad_inv_order << std::endl;
        std::cout << "  Zap Framing invalid ordering count: " << spans[span].sd_sad_zap.sad_inv_order << std::endl;
    }
    return (0);

invalidformat:
    return (-1);
}

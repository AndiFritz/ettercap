/*
    etterlog -- connections module

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

    $Header: /home/drizzt/dev/sources/ettercap.cvs/ettercap_ng/utils/etterlog/el_conn.c,v 1.2 2003/03/29 16:20:39 alor Exp $
*/

#include <el.h>
#include <ec_log.h>
#include <el_functions.h>

struct conn_list {
   struct ip_addr L3_src;
   struct ip_addr L3_dst;
   u_int16 L4_src;
   u_int16 L4_dst;
   u_char L4_proto;
   SLIST_ENTRY(conn_list) next;
};

static SLIST_HEAD(, conn_list) conn_list_head;

/* we can use the same struct */
static struct conn_list conn_target;

/* proto */

void conn_table(void);
static int insert_table(struct log_header_packet *pck);
void filcon_compile(char *conn);
int is_conn(struct log_header_packet *pck, int *versus);

/*******************************************/

/* analyze a packet log file */

void conn_table(void)
{
   struct log_header_packet pck;
   struct conn_list *c;
   int ret, count = 0;
   u_char *buf;
   char proto[5];
   char ipsrc[MAX_ASCII_ADDR_LEN];
   char ipdst[MAX_ASCII_ADDR_LEN];

   if (GBL.hdr.type == LOG_INFO)
      FATAL_MSG("LOG_INFO files don't contain connections !");
   
   
   fprintf(stdout, "\nCreating the connection table...\n");
  
   /* read the logfile */
   LOOP {
      ret = get_packet(&pck, &buf);

      /* on error exit the loop */
      if (ret != ESUCCESS)
         break;
      
      count += insert_table(&pck);
      
      SAFE_FREE(buf);
   }
  
   fprintf(stdout, "\nFound %d connection...\n\n", count);
   
   SLIST_FOREACH(c, &conn_list_head, next) {
      switch(c->L4_proto) {
         case NL_TYPE_TCP:
            strcpy(proto, "TCP");
            break;
         case NL_TYPE_UDP:
            strcpy(proto, "UDP");
            break;
      }
      
      ip_addr_ntoa(&c->L3_src, ipsrc);
      ip_addr_ntoa(&c->L3_dst, ipdst);
      
      fprintf(stdout, "%s: %s:%d <--> %s:%d\n", proto, ipsrc, ntohs(c->L4_src), 
                                                       ipdst, ntohs(c->L4_dst)); 
   }
   
   fprintf(stdout, "\n\n");

   return;
}

/*
 * insert a new connection in the list
 * if it isn't already there
 */

static int insert_table(struct log_header_packet *pck)
{
   struct conn_list *c;

   /* search if the connection is alread present */
   SLIST_FOREACH(c, &conn_list_head, next) {

      /* form source to dest */
      if (c->L4_proto == pck->L4_proto &&
          c->L4_src == pck->L4_src &&
          c->L4_dst == pck->L4_dst &&
          ip_addr_cmp(&c->L3_src, &pck->L3_src) &&
          ip_addr_cmp(&c->L3_dst, &pck->L3_dst))
         return 0;
      
      /* form dest to source */
      if (c->L4_proto == pck->L4_proto &&
          c->L4_src == pck->L4_dst &&
          c->L4_dst == pck->L4_src &&
          ip_addr_cmp(&c->L3_src, &pck->L3_dst) &&
          ip_addr_cmp(&c->L3_dst, &pck->L3_src))
         return 0;
   }

   /* not found in the list... add it */
   
   c = calloc(1, sizeof(struct conn_list));
   ON_ERROR(c, NULL, "Can't allocate memory");
   
   c->L4_proto = pck->L4_proto;
   c->L4_src = pck->L4_src;
   c->L4_dst = pck->L4_dst;
   
   memcpy(&c->L3_src, &pck->L3_src, sizeof(struct ip_addr));
   memcpy(&c->L3_dst, &pck->L3_dst, sizeof(struct ip_addr));
   
   SLIST_INSERT_HEAD(&conn_list_head, c, next);
   
   return 1;
}

/*
 * create the filter for the connection 
 */

void filcon_compile(char *conn)
{
#define MAX_TOK 5
   char valid[] = "1234567890.:TCPUDtcpud";
   struct in_addr ipaddr;
   char *tok[MAX_TOK];
   char *p;
   int i = 0;

   /* sanity check */ 
   if (strlen(conn) != strspn(conn, valid))
      FATAL_MSG("CONNECTION contains invalid chars !");

   /* CONN parsing */
   for(p=strsep(&conn, ":"); p != NULL; p=strsep(&conn, ":")) {
      tok[i++] = strdup(p);
      /* bad parsing */
      if (i > MAX_TOK) break;
   }

   if (i != MAX_TOK)
      FATAL_MSG("Incorrect number of token (PROTO:IP:PORT:IP:PORT) in CONNECTION !!");


   if (!strcasecmp(tok[0], "tcp"))
      conn_target.L4_proto = NL_TYPE_TCP;
   
   if (!strcasecmp(tok[0], "udp"))
      conn_target.L4_proto = NL_TYPE_UDP;
   

   /* source and dest ports */
   conn_target.L4_src = htons(atoi(tok[2]));
   conn_target.L4_dst = htons(atoi(tok[4]));

   /* source and dest ip */
   if (inet_aton(tok[1], &ipaddr) == 0)
      FATAL_MSG("Invalid IP address (%s)", tok[1]);

   ip_addr_init(&conn_target.L3_src, AF_INET, (char *)&ipaddr );
      
   if (inet_aton(tok[3], &ipaddr) == 0)
      FATAL_MSG("Invalid IP address (%s)", tok[3]);

   ip_addr_init(&conn_target.L3_dst, AF_INET, (char *)&ipaddr );
     
   /* free the data */
   for(i=0; i < MAX_TOK; i++)
      SAFE_FREE(tok[i]);
      
}

/*
 * return 1 if the packet is compliant with the filter
 */

int is_conn(struct log_header_packet *pck, int *versus)
{
   struct conn_list tmp;
   int proto = 0;
   int good = 0;
  
   memset(&tmp, 0, sizeof(struct conn_list));
   
   /* if the conn_target is not initialized, accept all */
   if (!memcmp(&conn_target, &tmp, sizeof(struct conn_list)))
      return 1;
   
   /* the protocol does not match */
   if (conn_target.L4_proto == pck->L4_proto)
      proto = 1;
   
   /*
    * we have to check if the packet is complying with the filter
    * specified by the users.
    */
 
   /* from source to dest */
   if ( ip_addr_cmp(&pck->L3_src, &conn_target.L3_src) &&
        ip_addr_cmp(&pck->L3_dst, &conn_target.L3_dst) &&
        pck->L4_src == conn_target.L4_src &&
        pck->L4_dst == conn_target.L4_dst &&
        /* the packet is from source, but we are interested only in dest */
        !GBL.only_dest ) {
      good = 1;
      *versus = VERSUS_SOURCE;
   }
   
   /* from dest to source */
   if ( ip_addr_cmp(&pck->L3_src, &conn_target.L3_dst) &&
        ip_addr_cmp(&pck->L3_dst, &conn_target.L3_src) &&
        pck->L4_src == conn_target.L4_dst &&
        pck->L4_dst == conn_target.L4_src &&
        /* the packet is from dest, but we are interested only in source */
        !GBL.only_source ) {
      good = 1;
      *versus = VERSUS_DEST;
   }
   
   /* check the reverse option */
   if (GBL.reverse ^ (good && proto) ) 
      return 1;
   else
      return 0;
   return 1;
}



/* EOF */

// vim:ts=3:expandtab

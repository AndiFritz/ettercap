
#ifndef EC_FILTER_H
#define EC_FILTER_H

/* 
 * this is the struct used by the filtering engine
 * it is the equivalent of a processor's instruction
 *
 * they are organized in an array and evaluated one 
 * at a time. the jump are absolute and the addressing
 * is done by the array position.
 *
 * we have to implement a struct to contain all the possible
 * operations:
 *
 *    offset == value
 *       e.g.  if (L4.proto == NL_TYPE_TCP)  
 * 
 *    offset = value8
 *    offset = value16
 *    offset = value32
 *       e.g.   L3.ip.ttl = 64
 *              L4.tcp.seq = 0xe77ee77e
 *              DATA.data+32 = 0xff 
 *
 *    search(where, "what")
 *       e.g.  search(DATA.data, "1.99");
 *
 *    replace(where, "what", "value")
 *       e.g.  replace(DATA.data, "1.99", "1.51");
 *
 *    log()
 *
 *    drop()
 *
 *    jump(address)
 */

#define MAX_FILTER_LEN  200

struct filter_op {
   char opcode;
      #define FOP_EXIT     0
      #define FOP_TEST     1
      #define FOP_FUNC     2
      #define FOP_JMP      3
      #define FOP_JTRUE    4
      #define FOP_JFALSE   5

   union {
      /* functions */
      struct {
         char opcode;
            #define FFUNC_SEARCH    0
            #define FFUNC_REGEX     1
            #define FFUNC_REPLACE   2
            #define FFUNC_LOG       3
            #define FFUNC_DROP      4
            #define FFUNC_MSG       5
            #define FFUNC_EXEC      6
         u_int8 level; 
         u_int8 value[MAX_FILTER_LEN];
         size_t value_len;
         u_int8 value2[MAX_FILTER_LEN];
         size_t value2_len;
      } func;
      
      /* tests */
      struct {
         u_int8  level;
         u_int8  size;
         u_int16 offset;
         u_int64 value;
         char string[MAX_FILTER_LEN];
         size_t string_len;
      } test;

      /* jumps */
      u_int16 jmp;
      
   } op;
};


/* exported functions */

extern int filter_engine(struct filter_op *fop, struct packet_object *po);

#endif

/* EOF */

// vim:ts=3:expandtab

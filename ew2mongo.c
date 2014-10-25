
/*
 *   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
 *   CHECKED IT OUT USING THE COMMAND CHECKOUT.
 *
 *    $Id: ew2mongo.c 5573 2013-06-14 03:05:30Z paulf $
 *
 *    Revision history:
 *     $Log$
 *     Revision 1.13  2009/09/24 21:45:58  stefan
 *     flush each line
 *
 *     Revision 1.12  2008/09/11 18:29:20  paulf
 *     fixed remote unknown remote instid and typeid when in verbose mode
 *
 *     Revision 1.11  2008/08/01 00:19:59  paulf
 *     fixed remote module id check when using verbose mode
 *
 *     Revision 1.10  2007/03/13 00:59:32  stefan
 *     minor doc addition
 *
 *     Revision 1.9  2007/03/13 00:42:32  stefan
 *     typo
 *
 *     Revision 1.8  2007/02/07 00:19:11  stefan
 *     help grammar
 *
 *     Revision 1.7  2007/02/02 19:06:30  stefan
 *     changed default behavior to what it was before, removed module verbosity for non-local modules
 *
 *     Revision 1.6  2006/12/01 01:24:22  stefan
 *     changed default to print names instead of numbers, with option to still print numbers the way it used to
 *
 *     Revision 1.5  2004/04/16 20:58:20  dietz
 *     Modified to skip printing TYPE_TRACEBUF2 and TYPE_TRACE2_COMP_UA binary messages
 *
 *     Revision 1.4  2001/02/14 00:43:32  dietz
 *     added fflush(stdout) so redirection of stdout will write to disk
 *
 *     Revision 1.3  2001/01/22 19:25:30  dietz
 *     Added printing of ascii messages.
 *
 *     Revision 1.2  2000/07/11 19:56:21  lucky
 *     Modified argument list: No more tracebuf file option -- this can be done more
 *     ellegantly with the sniffwave command.
 *     Added the possibility to sniff for message with particular logos, including
 *     wildcards. The user can specify INST_, MOD_, and TYPE_ strings desired,
 *     or omit those and only specify the ring to sniff. Therefore, ew2mongo RING_NAME
 *     is equivalent to ew2mongo RING_NAME INST_WILDCARD MOD_WILDCARD TYPE_WILDCARD.
 *
 *     Revision 1.1  2000/02/14 19:31:49  lucky
 *     Initial revision
 *
 *
 */


  /********************************************************************
   *                    ew2mongo - a debugging tool                  *
   *                                                                  *
   *  Command line program that latches onto a user-given transport   *
   *  ring and reads every message on that ring.  It prints the logo, *
   *  sequence # and length of the message, followed by the message   *
   *  itself (except for binary waveform messages) to the screen.     *
   *                                                                  *
   ********************************************************************/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <earthworm.h>
#include <transport.h>
#include <bson.h>
#include <mongoc.h>
#include <stdio.h>
#include <trace_buf.h>

#define MAX_SIZE MAX_BYTES_PER_EQ   /* Largest message size in characters */

/* Things to look up in the earthworm.h tables with getutil.c functions
 **********************************************************************/
static unsigned char InstId;        /* local installation id              */
static unsigned char InstWildCard;  /* wildcard for installations         */
static unsigned char ModWildCard;   /* wildcard for module                */
static unsigned char TypeWildCard;  /* wildcard for message type          */
static unsigned char TypeTraceBuf;  /* binary 1-channel waveform msg      */
static unsigned char TypeTraceBuf2; /* binary 1-channel waveform msg      */
static unsigned char TypeCompUA;    /* binary compressed waveform msg     */
static unsigned char TypeCompUA2;   /* binary compressed waveform msg     */
static unsigned char TypeMseed;     /* binary compressed mseed waveform msg     */
static unsigned char InstToGet;
static unsigned char TypeToGet;
static unsigned char ModToGet;

TracePacket     tpkt;
long * data;

// void write2mongo( MSG_LOGO logo, struct *client, long * data, long length, int inseq );

int main( int argc, char *argv[] )
{
   SHM_INFO inRegion;
   MSG_LOGO getlogo[1];
   MSG_LOGO logo;
   char     *inRing;           /* Name of input ring           */
   long     inkey;             /* Key to input ring            */
   mongoc_client_t *client;     /* Connection to mongodb client */
   mongoc_collection_t *collection;
   char  msg[4096];
   unsigned char inseq;        /* transport seq# in input ring */
   int      rc;
   int      no_flush = 0;	/*  flush ring buffer by default */
   long     length;
   char     verbose = 1;       /* verbose will print names     */
   char     ModName[MAX_MOD_STR+1];
   char     InstName[MAX_INST_STR+1];
   char     TypeName[MAX_TYPE_STR+1];

/* set up pointer to seperate trace data from offset
   ***********************/

data = (long *) ((char *) &tpkt + sizeof(TRACE2X_HEADER));

/* Check program arguments
   ***********************/
   if ( argc != 3 )
   {
      printf( "Usage:  ew2mongo <ringname> <mongo connection URI> \n" );
      return -1;
   }

   inRing  = argv[1];

/* Look up local installation id
   *****************************/
   if ( GetLocalInst( &InstId ) != 0 )
   {
      printf( "ew2mongo: error getting local installation id; exiting!\n" );
      return -1;
   }
   if ( GetInst( "INST_WILDCARD", &InstWildCard ) != 0 )
   {
      printf( "ew2mongo: Invalid installation name <INST_WILDCARD>" );
      printf( "; exiting!\n" );
      return -1;
   }

/* Look up module ids & message types in earthworm.h tables
   ****************************************************/
   if ( GetModId( "MOD_WILDCARD", &ModWildCard ) != 0 )
   {
      printf( "ew2mongo: Invalid module name <MOD_WILDCARD>; exiting!\n" );
      return -1;
   }
   if ( GetType( "TYPE_WILDCARD", &TypeWildCard ) != 0 )
   {
      printf( "ew2mongo: Message of type <TYPE_WILDCARD> not found in earthworm.d or earthworm_global.d; exiting!\n" );
      return -1;
   }
   if ( GetType( "TYPE_TRACEBUF", &TypeTraceBuf ) != 0 )
   {
      printf( "ew2mongo: Message of type <TYPE_TRACEBUF> not found in earthworm.d or earthworm_global.d; exiting!\n" );
      return -1;
   }
   if ( GetType( "TYPE_TRACEBUF2", &TypeTraceBuf2 ) != 0 )
   {
      printf( "ew2mongo: Message of type <TYPE_TRACEBUF2> not found in earthworm.d or earthworm_global.d; exiting!\n" );
      return -1;
   }
   if ( GetType( "TYPE_MSEED", &TypeMseed ) != 0 )
   {
      printf( "ew2mongo: Message of type <TYPE_MSEED> not found in earthworm.d or earthworm_global.d; warning!\n" );
      TypeMseed=0;
   }
   if ( GetType( "TYPE_TRACE_COMP_UA", &TypeCompUA ) != 0 )
   {
      printf( "ew2mongo: Message of type <TYPE_TRACE_COMP_UA> not found in earthworm.d or earthworm_global.d; exiting!\n" );
      return -1;
   }
   if ( GetType( "TYPE_TRACE2_COMP_UA", &TypeCompUA2 ) != 0 )
   {
      printf( "ew2mongo: Message of type <TYPE_TRACE2_COMP_UA> not found in earthworm.d or earthworm_global.d; exiting!\n" );
      return -1;
   }


   /* Initialize getlogo to all wildcards (get any message)
    ******************************************************/
      getlogo[0].type   = TypeWildCard;
      getlogo[0].mod    = ModWildCard;
      getlogo[0].instid = InstWildCard;

/* Look up transport region keys earthworm.h tables
   ************************************************/
   if( ( inkey = GetKey(inRing) ) == -1 )
   {
        printf( "ew2mongo: Invalid input ring name <%s>; exiting!\n",
                 inRing );
        return -1;
   }

/* Attach to input and output transport rings
   ******************************************/
   tport_attach( &inRegion,  inkey );

/* TODO: Open mongodb connection and collection, handle issues
   *********************************************/
   mongoc_init ();
   client = mongoc_client_new (argv[2]);
   // TODO instead of ew_msgs, the collection should be named after the wave ring
   collection = mongoc_client_get_collection (client, "meteor", "ew_msgs");
    
/* Flush all old messages from the ring
   ************************************/
   if (!no_flush) {
   	while( tport_copyfrom( &inRegion, getlogo, (short)1, &logo,
                          &length, msg, MAX_SIZE, &inseq ) != GET_NONE );
   }

/* Grab next message from the ring
   *******************************/
   while( tport_getflag( &inRegion ) != TERMINATE )
   {
      rc = tport_copyfrom( &inRegion, getlogo, (short)1, &logo,
                           &length, (char *) &tpkt, MAX_SIZE, &inseq );

      if ( rc == GET_OK )
      {
      /* This is the most likely case.  Output msg at bottom of if-elseif */
      }

      else if ( rc == GET_NONE )
      {
         sleep_ew( 200 );
         continue;
      }

      else if ( rc == GET_NOTRACK )
      {
         printf( "ew2mongo error in %s: no tracking for msg; i:%d m:%d t:%d seq:%d\n",
                  inRing, (int)logo.instid, (int)logo.mod, (int)logo.type, (int)inseq );
      }

      else if ( rc == GET_MISS_LAPPED )
      {
         printf( "ew2mongo error in %s: msg(s) overwritten; i:%d m:%d t:%d seq:%d\n",
                  inRing, (int)logo.instid, (int)logo.mod, (int)logo.type, (int)inseq );
      }

      else if ( rc == GET_MISS_SEQGAP )
      {
         printf( "ew2mongo error in %s: gap in msg sequence; i:%d m:%d t:%d seq:%d\n",
                  inRing, (int)logo.instid, (int)logo.mod, (int)logo.type, (int)inseq );
      }

      else if ( rc == GET_TOOBIG )
      {
         printf( "ew2mongo error in %s: input msg too big; i:%d m:%d t:%d seq:%d\n",
                  inRing, (int)logo.instid, (int)logo.mod, (int)logo.type, (int)inseq );
         continue;
      }
      if (verbose) {
          if (InstId == logo.instid && GetModIdName(logo.mod) != NULL) {
               strcpy(ModName, GetModIdName(logo.mod));
          } else {
               sprintf( ModName, "UnknownRemoteMod:%d", logo.mod);
          }
          if (GetInstName(logo.instid)==NULL) {
               sprintf(InstName, "UnknownInstID:%d", logo.instid);
          } else {
               strcpy(InstName, GetInstName(logo.instid));
          }
          if (GetTypeName(logo.type)==NULL) {
               sprintf(TypeName, "UnknownRemoteType:%d", logo.type);
          } else {
               strcpy(TypeName, GetTypeName(logo.type));
          }
          /* Print message logo names, etc. to the screen
          ****************************************/
          printf( "%d Received %s %s %s <seq:%3d> <Length:%6ld> and gonna write to mongo\n",
              (int)time (NULL), InstName, ModName, TypeName, (int)inseq, length );
              write2mongo( logo, collection, data, length, inseq );
      } else {
          /* Backward compatibility */
          /* Print message logo, etc. to the screen
          ****************************************/
          printf( "%d Received <inst:%3d> <mod:%3d> <type:%3d> <seq:%3d> <Length:%6ld> in compat part\n",
                  (int)time (NULL),  (int)logo.instid, (int)logo.mod, (int)logo.type, (int)inseq, length );
		  write2mongo( logo, collection, data, length, inseq );
      }
      fflush( stdout );


   /* Print numbers and names, etc. to the screen
    ****************************************
      printf( "%d Received <inst:%3d=%s> <mod:%3d=%s> <type:%3d=%s> <seq:%3d> <Length:%6ld>\n",
          (int)time (NULL),  (int)logo.instid, GetInstName(logo.instid), (int)logo.mod, GetModIdName(logo.mod), (int)logo.type, GetTypeName(logo.type), (int)inseq, length );*/



   /* Print any non-binary (non-waveform) message to screen
    *******************************************************/
      if( logo.type == TypeTraceBuf2 ) continue;
      if( logo.type == TypeCompUA2   ) continue;
      if( logo.type == TypeTraceBuf  ) continue;
      if( logo.type == TypeCompUA    ) continue;
      if( TypeMseed > 0 && logo.type == TypeMseed    ) continue;

      msg[length] = '\0'; /* Null-terminate message */
      printf( "%s\n", msg );
      fflush( stdout );
   }

/* Detach from shared memory, mongodb, and terminate
   ***********************************************/
   tport_detach( &inRegion );
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
   return 0;
}

void write2mongo(MSG_LOGO logo, mongoc_collection_t * collection, long * data, long length, int inseq){

    mongoc_cursor_t *cursor;
    bson_error_t error;
    bson_oid_t oid;
    bson_t *doc;
	if (logo.type == TypeTraceBuf2){
    doc = bson_new ();
    bson_oid_init (&oid, NULL);
    BSON_APPEND_OID (doc, "_id", &oid);
//      BSON_APPEND_UTF8 (doc, "msgtype", (char)logo.type);
    BSON_APPEND_INT32 (doc, "nsamp", tpkt.trh.nsamp);
    /* libbson expects a int64 in ms since epoch. Giving it a double in s instead. */
    BSON_APPEND_DATE_TIME (doc, "starttime", (int64_t)tpkt.trh.starttime * 1000);
    BSON_APPEND_DATE_TIME (doc, "endtime", (int64_t)tpkt.trh.endtime * 1000);
    BSON_APPEND_UTF8 (doc, "sta", (const char *)&tpkt.trh.sta);
    BSON_APPEND_UTF8 (doc, "chan", (const char *)&tpkt.trh.chan);
    BSON_APPEND_UTF8 (doc, "net", (const char *)&tpkt.trh.net);
    BSON_APPEND_UTF8 (doc, "msgmod", (const char *)GetModIdName(logo.mod));
    BSON_APPEND_DOUBLE (doc, "samprate", tpkt.trh.samprate);
    BSON_APPEND_UTF8 (doc, "dtype", (const char *)&tpkt.trh.datatype);

//     BSON_APPEND_UTF8 (doc, "msginst", (int)logo.instid);

    bson_append_binary (doc, "trace", -1, BSON_SUBTYPE_BINARY, (char *)data, length - sizeof(TRACE2X_HEADER));
    BSON_APPEND_INT32 (doc, "seq", inseq);

    if (!mongoc_collection_insert (collection, MONGOC_INSERT_NONE, doc, NULL, &error)) {
        printf ("%s\n", error.message);
    }
    bson_destroy (doc);
    }
}
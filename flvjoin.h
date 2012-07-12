#define PROG_NAME    "flvjoin"
#define PROG_VERSION "0.92"

struct FLVpacket
{
   unsigned char type;
   unsigned int datasize, timestamp, streamid;
   unsigned char *data;
   unsigned int backptr;
};


/* metadata.c */
struct FLVpacket *generate_metadata_packet(FILE *);
int extract_metadata(struct FLVpacket *);
void write_metadata(FILE *, unsigned int);

#include "data_conv.h"

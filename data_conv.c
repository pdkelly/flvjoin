/* 
    data_conv.c
    Functions to convert to and from FLV encoding of various data types in a
    platform-independent manner
    by Paul Kelly
    Copyright (C) 2007-09 Radiomonitor Ltd.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include <string.h> /* for memcpy() */

static short byte_order_test = 1;
#define IS_BIG_ENDIAN    (((unsigned char *) (&byte_order_test))[0] == 0)

double conv_double(const unsigned char *ptr)
{
   if( IS_BIG_ENDIAN )
       return *(double *)ptr;
   else
   {
       int i = 0, j = sizeof(double);
       unsigned char doublebuff[sizeof(double)];

       /* Swap endian-ness */
       while( --j >= 0 )
           doublebuff[j] = ptr[i++];

       return *(double *)doublebuff;
   }
}

/*
 * conv_ui32()
 * 
 * Converts the number contained in the 4 bytes starting at memory location
 * "ptr" from the UI32 byte-stream format used in FLV files (i.e. big-endian
 * order) to a C unsigned integer type.
 * 
 * Returns an unsigned integer containing the converted number.
 */
unsigned int conv_ui32(const unsigned char *ptr)
{
   if( IS_BIG_ENDIAN )
       return *(unsigned int *)ptr;
   else
   {
       int i = 0, j = 4;
       unsigned char intbuff[4];

       /* Swap endian-ness */
       while( --j >= 0 )
           intbuff[j] = ptr[i++];

       return *(unsigned int *)intbuff;
   }
}

/*
 * conv_ui24()
 * 
 * Converts the number contained in the 3 bytes starting at memory location
 * "ptr" from the UI24 byte-stream format used in FLV files to a C unsigned 
 * integer type, including "highbyte" as the high byte of a 4-byte unsigned 
 * integer - thus allowing the function to also be used to convert FLV 
 * timestamp values stored in UI24 + high byte format. For other uses 
 * "highbyte" should be set to 0.
 * 
 * Returns an unsigned integer containing the converted number.
 */
unsigned int conv_ui24(const unsigned char *ptr, unsigned char highbyte)
{
   unsigned char intbuff[4];
   int i = 0, j;

   if( IS_BIG_ENDIAN )
   {
       intbuff[0] = highbyte;      
       memcpy(&intbuff[1], ptr, 3);
   }   
   else
   {
       intbuff[3] = highbyte;
       /* Swap endian-ness */
       for( j = 2; j >= 0; j-- )
           intbuff[j] = ptr[i++];
   }

   return *(unsigned int *)intbuff;
}

unsigned short conv_ui16(const unsigned char *ptr)
{
   if( IS_BIG_ENDIAN )
       return *(unsigned short *)ptr;
   else
   {
       unsigned char intbuff[2];

       /* Swap endian-ness */
       intbuff[0] = ptr[1];
       intbuff[1] = ptr[0];

       return *(unsigned short *)intbuff;
   }
}

short conv_si16(const unsigned char *ptr)
{
   if( IS_BIG_ENDIAN )
       return *(short *)ptr;
   else
   {
       unsigned char intbuff[2];

       /* Swap endian-ness */
       intbuff[0] = ptr[1];
       intbuff[1] = ptr[0];

       return *(short *)intbuff;
   }
}

unsigned char *format_double(double number)
{
    static unsigned char outbuff[sizeof(double)];
    int i, j = 0;

    if( IS_BIG_ENDIAN )
        memcpy(outbuff, (unsigned char *)&number, sizeof(double));
    else
    {
        for( i = sizeof(double) - 1; i >= 0; i-- )
            outbuff[j++] = ((unsigned char *) &number)[i];
    }

    return outbuff;   
}

/*
 * format_ui32()
 * 
 * Converts the 4-byte unsigned integer "number" to the UI32 byte-stream 
 * format used in FLV files, i.e. big-endian order.
 * 
 * Returns a pointer to a statically-allocated 4-character buffer containing 
 * the bytes in the correct order for writing to the FLV bitstream.
 */
unsigned char *format_ui32(unsigned int number)
{
    static unsigned char outbuff[4];
    int i, j = 0;

    if( IS_BIG_ENDIAN )
        memcpy(outbuff, (unsigned char *)&number, 4);
    else
    {
        for( i = 3; i >= 0; i-- )
            outbuff[j++] = ((unsigned char *) &number)[i];
    }

    return outbuff;   
}

/*
 * format_ui24()
 * 
 * Converts the 4-byte unsigned integer "number" to the UI24 byte-stream 
 * format used in FLV files, i.e. the 3 lower bytes in big-endian order. 
 * The high byte is also placed in the memory location immediately following 
 * the 3 low bytes; this enables this function to also be used for formatting 
 * an FLV timestamp value in the UI24 + high byte format used. Other uses can
 * simply ignore the 4th byte.
 * 
 * Returns a pointer to a statically-allocated 4-character buffer containing 
 * the bytes in the correct order for writing to the FLV bitstream.
 */
unsigned char *format_ui24(unsigned int number)
{
    static unsigned char outbuff[4];
    int i, j = 0;

    if( IS_BIG_ENDIAN )
    {
        outbuff[3] = ((unsigned char *)&number)[0];
        for( i = 1; i < 4; i++ )
            outbuff[j++] = ((unsigned char *)&number)[i];
    }
    else
    {
        outbuff[3] = ((unsigned char *) &number)[3];
        for( i = 2; i >= 0; i-- )
            outbuff[j++] = ((unsigned char *) &number)[i];
    }

    return outbuff;   
}

unsigned char *format_ui16(unsigned short number)
{
    static unsigned char outbuff[2];

    if( IS_BIG_ENDIAN )
        memcpy(outbuff, (unsigned char *)&number, 2);
    else
    {
        outbuff[0] = ((unsigned char *) &number)[1];
        outbuff[1] = ((unsigned char *) &number)[0];
    }

    return outbuff;   
}


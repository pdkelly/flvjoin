/* flvparse.c */
/*
    Simple Flash Video file parser by Paul Kelly.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "data_conv.h"

unsigned char *parse_script_object(unsigned char *);
unsigned char *parse_script_variable(unsigned char*);
unsigned char *parse_script_string(unsigned char *, unsigned int);

static void parse_file(FILE *infile)
{
    size_t buffsize = 1024, size;
    unsigned char *buff = malloc(buffsize);

    {
        /* Look for the header at the start of the file */
        int header_length = 9, i;

        size = fread( buff, 1, header_length, infile );

        for (i = 0; i <  3; i++) /* Print 3 signature bytes */
            putchar(buff[i]);

        printf("v.");
        printf("%d",buff[3]); /* FLV version */

        printf("\nAudio present: %s", (buff[4] & 4)? "Yes" : "No");
        printf("\nVideo present: %s", (buff[4] & 1)? "Yes" : "No");

        header_length -= conv_ui32(&buff[5]);
        printf("\nExtra Header length: %d bytes\n", header_length );

        /* Skip over any extra header bytes if present */
        fseek( infile, header_length, SEEK_CUR );
        printf("----End of Header----\n");
    }
   
    while(1)
    {	
        unsigned int prev_tag_length;
       
        /* Read back-pointer and exit if there's nothing after it */
        size = fread( buff, 1, 4, infile );
        prev_tag_length = conv_ui32(buff);
       
        printf("Prev. tag length: %d bytes\n", prev_tag_length);
       
        if( feof(infile) )
	{
	    fprintf(stderr,"EOF after back pointer; exiting.\n");
	    break;
	}       
       
        /* Read the tag header */
	{   
	    unsigned char tag_type;
	    unsigned int datasize, timestamp;
	   
            size = fread( buff, 1, 11, infile );
	   
	    tag_type = buff[0];
	    datasize = conv_ui24(&buff[1], 0);
	    timestamp = conv_ui24(&buff[4], buff[7]);

	    switch(tag_type)
	    {
	        case 8:
	            printf( "Audio Tag, ");
	            break;
	        case 9:
	            printf( "Video Tag, ");
	            break;
	        case 18:
	            printf( "Script Tag, ");
	            break;
	        default:
	            printf( "Undefined Tag (Type %d), ", tag_type);
	            break;
	    }
	    printf( "%d bytes. Timestamp %dms.\n", datasize, timestamp);

	    if( tag_type == 18 )
	    {
	        /* Read and parse script tag data */
	        unsigned char object_end[] = { 2, 0, 0, 9 };
	        unsigned char *pos;
	       
	        if( datasize > buffsize )
		    buff = realloc( buff, datasize );
	        fread( buff, 1, datasize, infile );

	        pos = buff;
	        do {		    
	            pos = parse_script_object(pos);
		   
		    if( memcmp(pos, object_end, 4) != 0 )
                        printf( "WARNING: Script Object closing bytes missing.\n");
		    else
		        /* Skip over closing bytes if present */
		        pos += 4;

		    printf("--Script Object End\n");
	        } while( (pos - buff) < datasize );	        
	    }
	    else
	        /* Skip over payload */
	        fseek( infile, datasize, SEEK_CUR );
	}       
       
    }

    return;
}

unsigned char *parse_script_object(unsigned char *pos)
{
    unsigned char marker = *(pos++);
    unsigned char variable_end[] = { 0, 0, 9 };

    printf("--Script Object Start\n");
   
    if( marker != 2 )
    {	
        printf( "WARNING: Script Object Marker Byte missing.\n");
        pos--;
    }   

    printf( "Object Name: ");
    pos = parse_script_string(pos + 2, conv_ui16(pos));
    printf( "\tType: ");    
    pos = parse_script_variable(pos);

    if( memcmp(pos, variable_end, 3) != 0 )
        printf( "WARNING: Script variable closing bytes missing.\n");
    else
	/* Skip over closing bytes if present */
        pos += 3;

    return pos;
}

unsigned char *parse_script_variable(unsigned char *pos)
{
    unsigned char variable_type = *(pos++);
   
    switch( variable_type )
    {
        case 0:
            printf("Number\tValue: %.2f\n", conv_double(pos));       
            pos += sizeof(double);
            break;
        case 1:
            printf("Boolean\tValue: %d\n", *pos);
            pos++;
            break;
        case 2:
            printf("String\tValue: ");
            pos = parse_script_string(pos + 2, conv_ui16(pos));
            printf("\n");
            break;
        case 3:
            printf("Object\n");
            pos = parse_script_object(pos);
            break;
        case 4:
            printf("MovieClip\n");
            break;
        case 5:
            printf("Null\n");
            break;
        case 6:
            printf("Undefined\n");
            break;
        case 7:
            printf("Reference\tValue: %d\n", conv_ui16(pos));
            pos += 2;
            break;
        case 8:
            printf("ECMA Array\t");
	    {
	        int array_length = conv_ui32(pos);
		int count;
		       
		pos += 4;
		printf("Length: %d variables\n", array_length);
		    
		for( count = 0; count < array_length; count++ )
		{
                    printf( "Variable %d\tName: ", count);
		    pos = parse_script_string(pos + 2, conv_ui16(pos));
                    printf( "\tType: ");    
		    pos = parse_script_variable(pos);
		}		       
	    }
            break;
        case 10:
            printf("Script Array\t");
	    {
	        int array_length = conv_ui32(pos);
		int count;
		       
		pos += 4;
		printf("Length: %d variables\n", array_length);
		    
		for( count = 0; count < array_length; count++ )
		{		    
                    printf( "Variable %d\tType: ", count);
		    pos = parse_script_variable(pos);
		}	       
	    }
            break;
        case 11:
            printf("Date\t");
	    {
	        time_t timestamp = conv_double(pos) / 1000; /* value is millisecs since epoch */
	        short tz_offset;

	        pos += sizeof(double);
	        tz_offset = conv_si16(pos);
	        pos +=2;
	       
	        printf("Value: %s\tTimezone: %+g\n", asctime(gmtime(&timestamp)),
		       (double) tz_offset / 60);
	    }
            break;
        case 12:
            printf("Long String\tValue: ");
            pos = parse_script_string(pos + 2, conv_ui32(pos));
            break;
        default:
            printf("ERROR\n");
            break;
    }

    return pos;
}


unsigned char *parse_script_string(unsigned char *pos, unsigned int string_length)
{   
    unsigned int count;
   
    for( count = 0; count < string_length; count++)
        putchar((char)pos[count]);   
	       
    pos += string_length;
   
    return pos;
}


int main(int argc, char **argv)
{
    FILE *fd;

    if(argc > 1)
    {
	fd = fopen(argv[1], "rb");
	if(!fd)
	{
	    fprintf(stderr, "Error opening file %s\n", argv[1]);
	    exit(1);
	}
    } 
    else
	fd = stdin;

    parse_file(fd);
   
    fclose(fd);

    return 0;
}

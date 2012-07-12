/* 
    metadata.c
    Seamless Flash Video file joining utility by Paul Kelly.
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
#include <errno.h>

#include "flvjoin.h"

struct FLVmetadata
{
    double duration, width, height, framerate, videocodecid, audiosamplerate, audiosamplesize;
    char stereo;
    double audiocodecid, filesize;
    long ofs_duration, ofs_width, ofs_height, ofs_framerate, ofs_videocodecid;
    long ofs_audiosamplerate, ofs_audiosamplesize, ofs_stereo, ofs_audiocodecid, ofs_filesize;
};

static void parse_script_object(unsigned char **);
static double parse_number(unsigned char**);
static unsigned char *parse_script_string(unsigned char *, unsigned int, char *);
static void add_meta_item(const char *, double);

static struct FLVmetadata meta;

/* 
 * put_string()
 * 
 * Encode string "string" in FLV string encoding and write it to the file stream 
 * described by "fd".
 */
static void put_string(char *string, FILE *fd)
{
    unsigned short len = strlen(string);
    /* Write length as unsigned short */
    fwrite(format_ui16(len), 2, 1, fd);
    /* Write string without terminating NULL-byte */
    fwrite(string, 1, len, fd);
   
    return;
}

/*
 * put_double()
 * 
 * Encode double-precision number "number" in FLV double encoding and write it
 * to the file stream described by "fd".
 */
static void put_double(double number, FILE *fd)
{
    fputc(0, fd); /* double marker byte */
    if(fwrite(format_double(number), sizeof(double), 1, fd) != 1)
        fprintf(stderr, "Error writing double to file: %s\n", strerror(errno));
   
    return;
}

/*
 * put_boolean()
 * 
 * Encode the boolean value "value" in FLV boolean encoding and write it to
 * the file stream described by "fd".
 */
static void put_boolean(char value, FILE *fd)
{
    fputc(1, fd); /* boolean marker byte */
    fputc(value, fd);
   
    return;
}

/*
 * generate_metadata_packet()
 * 
 * Create an FLV packet containing a Script Data Object with placeholders
 * for various metadata fields. Store the offsets of these fields within
 * the file so that we can rewind to write in the correct values before
 * closing the file.
 */
struct FLVpacket *generate_metadata_packet(FILE *fd)
{
    unsigned char variable_end[] = { 0, 0, 9 };
    char buff[255];
    long currpos = ftell(fd) + 11; /* Take account of size of packet header */
    long data_len;
    unsigned char *data;
    struct FLVpacket *packet = malloc(sizeof(struct FLVpacket));
   
    /* First write metadata to temporary file, then read it back in and form a proper
     * FLVpacket with it once we know the full data size */
   
    FILE *tmp = tmpfile();
   
    fputc(2, tmp); /* String object marker byte */
    put_string("onMetaData", tmp);
    fputc(8, tmp); /* ECMA array marker byte */
    fwrite(format_ui32(11), 4, 1, tmp); /* our array has 11 items */
    put_string("duration", tmp);
    meta.ofs_duration = currpos + ftell(tmp); /* save location to write to for later */
    put_double(0, tmp);
    put_string("width", tmp);
    meta.ofs_width = currpos + ftell(tmp);
    put_double(0, tmp);
    put_string("height", tmp);
    meta.ofs_height = currpos + ftell(tmp);
    put_double(0, tmp);
    put_string("framerate", tmp);
    meta.ofs_framerate = currpos + ftell(tmp);
    put_double(0, tmp);
    put_string("videocodecid", tmp);
    meta.ofs_videocodecid = currpos + ftell(tmp);
    put_double(0, tmp);
    put_string("audiosamplerate", tmp);
    meta.ofs_audiosamplerate = currpos + ftell(tmp);
    put_double(0, tmp);
    put_string("audiosamplesize", tmp);
    meta.ofs_audiosamplesize = currpos + ftell(tmp);
    put_double(0, tmp);
    put_string("stereo", tmp);
    meta.ofs_stereo = currpos + ftell(tmp);
    put_boolean(0, tmp);
    put_string("audiocodecid", tmp);
    meta.ofs_audiocodecid = currpos + ftell(tmp);
    put_double(0, tmp);
    put_string("filesize", tmp);
    meta.ofs_filesize = currpos + ftell(tmp);
    put_double(0, tmp);
    put_string("metadatacreator", tmp);
    sprintf(buff, "%s v%s", PROG_NAME, PROG_VERSION);
    fputc(2, tmp); /* String object marker byte */
    put_string(buff, tmp);
    fwrite(variable_end, 1, 3, tmp);
   
    data_len = ftell(tmp);
    data = malloc(data_len);
    rewind(tmp);
    fread(data, 1, data_len, tmp);
    fclose(tmp);
   
    packet->type = 18; /* Script data object */
    packet->datasize = data_len;
    packet->timestamp = 0;
    packet->streamid = 0;
    packet->data = data;
    packet->backptr = data_len + 11;
   
    return packet;
}

/*
 * extract_metadata()
 * 
 * If FLV data packet "packet" contains a script data object, parse the
 * script data and, if any of one a of a number of pre-defined data fields
 * are present, store the data values as meta data.
 */
int extract_metadata(struct FLVpacket *packet)
{
    unsigned char *pos = packet->data;
    unsigned char marker;
    int found_metadata_marker = 0;

    if(packet->type != 18) /* Script Data Object */
        return 0;
   
    marker = *(pos++);
    if( marker != 2 )
        /* Script Object Marker Byte missing */
        pos--;

    /* Check for onMetaData object name; non-watertight check
     * for buffer over-run */
    while(pos - packet->data <= packet->datasize - 6)
    {	
        char buff[1024];
        double val;

        pos = parse_script_string(pos + 2, conv_ui16(pos), buff);
        if(strcmp(buff, "onMetaData") == 0)
            found_metadata_marker = 1;
        val = parse_number(&pos);
        add_meta_item(buff, val);
    }

    /* If we don't find an "onMetaData" string we've still processed and populated
     * our meta object with any valid tags found, however any later script object
     * packets may override these. If the current script object contains the "onMetaData"
     * string anywhere, then we'll consider this the definitive metadata and will
     * not process any further script data object packets. */

    return found_metadata_marker;
}

/*
 * add_meta_item()
 * 
 * Check a string pair/value combination, and if the string corresponds
 * to one of a specified set of metadata fields, store the value in
 * extern struct "meta".
 */
static void add_meta_item(const char *name, double value)
{
    if(value == -1)
        return;
    if(strcmp(name, "width") == 0)
        meta.width = value;
    else if(strcmp(name, "height") == 0)
        meta.height = value;
    else if(strcmp(name, "framerate") == 0)
        meta.framerate = value;
    else if(strcmp(name, "videocodecid") == 0)
        meta.videocodecid = value;
    else if(strcmp(name, "audiosamplerate") == 0)
        meta.audiosamplerate = value;
    else if(strcmp(name, "audiosamplesize") == 0)
        meta.audiosamplesize = value;
    else if(strcmp(name, "stereo") == 0)
        meta.stereo = (char)value;
    else if(strcmp(name, "audiocodecid") == 0)
        meta.audiocodecid = value;

    return;
}

/*
 * write_metadata()
 * 
 * Calculate duration and filesize based on the timestamp and file descriptor
 * passed, and write the contents of extern struct "meta" to the file at the
 * byte offsets that were stored by generate_metadata_packet().
 */
void write_metadata(FILE *fd, unsigned int timestamp)
{
    meta.duration = (double)timestamp / 1000;
    meta.filesize = (double)ftell(fd);

    fseek(fd, meta.ofs_duration, SEEK_SET);
    put_double(meta.duration, fd);
    fseek(fd, meta.ofs_width, SEEK_SET);
    put_double(meta.width, fd);
    fseek(fd, meta.ofs_height, SEEK_SET);
    put_double(meta.height, fd);
    fseek(fd, meta.ofs_framerate, SEEK_SET);
    put_double(meta.framerate, fd);
    fseek(fd, meta.ofs_videocodecid, SEEK_SET);
    put_double(meta.videocodecid, fd);
    fseek(fd, meta.ofs_audiosamplerate, SEEK_SET);
    put_double(meta.audiosamplerate, fd);
    fseek(fd, meta.ofs_audiosamplesize, SEEK_SET);
    put_double(meta.audiosamplesize, fd);
    fseek(fd, meta.ofs_stereo, SEEK_SET);
    put_boolean(meta.stereo, fd);
    fseek(fd, meta.ofs_audiocodecid, SEEK_SET);
    put_double(meta.audiocodecid, fd);
    fseek(fd, meta.ofs_filesize, SEEK_SET);
    put_double(meta.filesize, fd);

    return;
}

/*
 * parse_script_object()
 * 
 * Parse an FLV script object and save it as metadata if a relevant name/value
 * combination is found.
 * Update position pointer "pos" to point to the next piece of data after
 * the object.
 */
static void parse_script_object(unsigned char **pos)
{
    unsigned char marker = *(*pos)++;
    unsigned char variable_end[] = { 0, 0, 9 };
    char buff[1024];
    double val;

    if( marker != 2 )
        /* Script Object Marker Byte missing */
        (*pos)--;

    *pos = parse_script_string(*pos + 2, conv_ui16(*pos), buff);
    val = parse_number(pos);
    add_meta_item(buff, val);

    if( memcmp(*pos, variable_end, 3) == 0 )
	/* Skip over closing bytes if present */
        *pos += 3;

    return;
}

/*
 * parse_number
 * 
 * Parse an FLV script variable and return a double-precision number if
 * a numerical value was able to be extracted, otherwise return -1.
 * If any name/value combinations are extracted, save them as metadata if
 * appropriate.
 * Update position pointer "pos" to point to the next piece of data after
 * the variable.
 */
static double parse_number(unsigned char **pos)
{  
    unsigned char variable_type = *(*pos)++;
    double value = -1;

    switch( variable_type )
    {
        case 0: /* double */
            value = conv_double(*pos);
            *pos += sizeof(double);
            break;
        case 1: /* boolean */
            value = (double)**pos;
            (*pos)++;
            break;
        case 2: /* string */
            *pos = parse_script_string(*pos + 2, conv_ui16(*pos), NULL);
            break;
        case 3: /* entire script object */
            parse_script_object(pos);
            break;
        case 7: /* reference */
            value = conv_ui16(*pos);
            *pos += 2;
            break;
        case 8: /* ECMA array */
	    {
	        int array_length = conv_ui32(*pos);
		int count;
		       
		*pos += 4;
		for( count = 0; count < array_length; count++ )
		{
	            char buff[1024];
		    double val;

		    *pos = parse_script_string(*pos + 2, conv_ui16(*pos), buff);
		    val = parse_number(pos);
                    add_meta_item(buff, val);
		}		       
	    }
            break;
        case 10: /* script array */
	    {
	        int array_length = conv_ui32(*pos);
		int count;
		       
		*pos += 4;
		for( count = 0; count < array_length; count++ )
		    value = parse_number(pos);
	    }
            break;
        case 11: /* date */
            value = conv_double(*pos) / 1000; /* value is millisecs since epoch */
	    *pos += sizeof(double);
	    *pos += 2; /* TZ offset */
            break;
        case 12: /* long string */
            *pos = parse_script_string(*pos + 2, conv_ui32(*pos), NULL);
            break;
        default: /* unhandled; will probably result in corruption */
            fprintf(stderr, "WARNING: Unhandled script variable type %d\n", variable_type);
            break;
   }

    return value;
}

/*
 * parse_script_string()
 * 
 * Decode an FLV string object and store the decoded string in buffer "buff"
 * (buff may be NULL; in which case the decoded string is not saved). Update
 * position pointer "pos" to point to the next piece of data after the string.
 */
static unsigned char *parse_script_string(unsigned char *pos, unsigned int string_length, char *buff)
{   
    if(buff)
    {
        unsigned int count;
        for( count = 0; count < string_length; count++)
            buff[count] = (char)pos[count];
        buff[count] = '\0';
    }
	       
    pos += string_length;
   
    return pos;
}


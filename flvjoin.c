/* 
    flvjoin.c
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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "flvjoin.h"

/* Maximum length for input and output filenames (including full path) */
#define MAX_NAME_LEN 1024

int quiet;
static int no_meta;
static int frame_interval = 100;

static char filepath[MAX_NAME_LEN];
static FILE *outfile = NULL;

static struct FLVpacket seq_header_pkt;

static unsigned int last_video_timestamp;
static long last_audio_timestamp = -1;
static unsigned int last_packet_size;

static void write_flv_header(void);

static void append_file(const char *, unsigned int, unsigned int);
static void buffer_packet(struct FLVpacket *, long, char);
static void write_packet(struct FLVpacket *, long);

static void open_output(const char *);
static void write_output(unsigned char *, size_t);
static void close_output(void);

/*
 * main()
 * 
 * Parse command-line options, perform checks on output filename.
 * If option to write header was specified, open output file for writing, write 
 * FLV header to the file, close and exit.
 * Otherwise open output file for appending, read filenames from stdin one at a
 * time and process each one with append_file(). Finally close output file.
 */
int main (int argc, char ** argv)
{
    char buffer[2*MAX_NAME_LEN];
    int audio_bitrate = 32000;
    int opt;

    filepath[0] = '\0';

    /* Parse command-line options */
    while ( (opt = getopt(argc, argv, "o:f:b:ndqh")) != -1 ) 
    {
        switch (opt)
        {
            case 'o':
                strncpy(filepath, optarg, sizeof(filepath));
                break;
            case 'f':
                frame_interval = (int)(0.5 + 1000 / atof(optarg));
                break;
            case 'b':
                audio_bitrate = atoi(optarg);
                break;
            case 'n':  
                no_meta = 1;
                break;
            case 'd': /* retained for backward compatibility */
                exit(EXIT_SUCCESS);
            case 'q':  
                quiet = 1;
                break;
            case 'h':
            default:
	        fprintf(stderr,"%s v%s\n", PROG_NAME, PROG_VERSION);
                fprintf(stderr,"\nSynopsis: Reads a list of FLV files (with optional in-point and out-point)\n");
                fprintf(stderr,"from standard input and joins them together into one larger FLV file.\n\n");
                fprintf(stderr,"Usage: %s -o <filename> [-f <framerate>] [-b <bitrate>] [-n] [-q] [-h]\n\n", PROG_NAME);
                fprintf(stderr,"   -o <filename>   Output File (- for stdout)\n");
                fprintf(stderr,"   -f <framerate>  Video frame rate in frames per second (default %.2f)\n", 1000.0 / frame_interval);
                fprintf(stderr,"   -b <bitrate>    Audio bitrate in bits per second (default %d)\n", audio_bitrate);
                fprintf(stderr,"   -n              Don't write metadata to output file\n");
                fprintf(stderr,"   -q              Don't display progress information\n");
                fprintf(stderr,"   -h              Display this usage message and exit\n");
                fprintf(stderr,"\n");
                exit(0);
        }
    }

    if( strlen(filepath) == 0 )
    {
        fprintf(stderr, "ERROR: Output file must be specified with the -o option. (Use - for stdout).\n");
        exit(1);
    }

    {
        struct stat s;
        struct FLVpacket *packet;
        
        if( strcmp(filepath, "-") != 0 && stat(filepath, &s) == 0 )
        {
            fprintf(stderr,"ERROR: File %s exists; won't write header.\n", filepath);
            exit(1);
        }       
        open_output("wb"); /* Open for writing */
        write_flv_header();
        /* Write blank metadata */
        if(!no_meta)
	{
            packet = generate_metadata_packet(outfile);
            write_packet(packet, 0);
	}
    }

    /* Read an input filename at a time from stdin and append to output */
    while( fgets(buffer, sizeof(buffer), stdin) )
    {
        int num_params;
        double mark_in, mark_out;
        char infile[MAX_NAME_LEN];
        char *newline = strchr(buffer, '\n');
        if(newline) /* Remove newline character */
            *newline = '\0';

        num_params = sscanf(buffer, "%1023s %lf %lf", infile, &mark_in, &mark_out);
        if(num_params < 3)
	    mark_out = 99999;
        if(num_params < 2)
	    mark_in = 0;
        append_file(infile, (unsigned int)(0.5 + mark_in*1000), (unsigned int)(0.5 + mark_out*1000));
    }

    /* Determine file duration based on last timestamp and the duration of that packet */
    if(!no_meta)
    {
	unsigned int duration;
       
        /* Rewind and write metadata */
        if(!quiet)
            fprintf(stderr, "Writing metadata...\n");

        if(last_video_timestamp >= last_audio_timestamp)
	    duration = last_video_timestamp + frame_interval;
        else
	    duration = last_audio_timestamp + (unsigned int)(0.5 + 1000.0 * last_packet_size * 8 / audio_bitrate);

        write_metadata(outfile, duration);
    }

    if( !quiet )
        fprintf(stderr, "Closing output file %s\n", filepath);
    close_output();

    exit(0);
}

/*
 * write_flv_header()
 * 
 * Write the standard 13-byte header found in all FLV files to the stream
 * specified by "outfile".
 * This header specifies that the file contains both audio and video streams,
 * i.e. byte nymber 5 is (0x4 | 0x1) = 0x5.
 *                          ^     ^
 *                  audio---|     |---video
 */
static void write_flv_header(void)
{
    const unsigned char header[] = {'F', 'L', 'V', 1, 5, 0, 0, 0, 9, 0, 0, 0, 0};

    if(!quiet)
        fprintf(stderr,"Writing FLV header to %s\n", filepath);

    fwrite( header, 1, sizeof(header), outfile );

    return;
}      

/*
 * append_file()
 * 
 * Opens the file "filename" for reading. Parses the FLV header and checks
 * everything looks normal. Then reads FLV data packets from the file
 * continuously in a loop until no more data can be read from the file.
 * The header of each FLV packet is parsed and the various fields stored in an 
 * FLVpacket struct. Non-video or audio packets (e.g. metadata packets) are 
 * skipped over and not parsed any further than the header. Video and audio
 * packets have their data payload and closing back pointer also stored in 
 * memory.
 * If the starting timestamp for the current output file has already been 
 * determined, write_packet() is then called to write the packet to the output
 * file. Otherwise the packets are buffered using buffer_packet() until the
 * first video packet has been read from the input file. The starting timestamp
 * is calculated based on the timestamp of the first video packet, the
 * video framerate, and the timestamp of the last video packet read from the
 * previous input file. The packet buffer is then flushed and operation
 * reverts to simply reading packets from input and writing to output.
 * When no more data can be read from the input file, it is closed and the 
 * function returns.
 */
static void append_file(const char *filename, unsigned int mark_in, unsigned int mark_out)
{
    static unsigned char *buff;
    static size_t buffsize = 13;
    static char first_time = 1;
    static char metadata_extracted;
    long file_start_timestamp = -999999;
    long first_keyframe_timestamp = -1;
    unsigned int lastfile_video_timestamp = last_video_timestamp;
    unsigned char signature[] = { 'F', 'L', 'V' };
    FILE *infile;

    if(!quiet)
        fprintf(stderr, "Opening \"%s\"\n", filename);

    if( !(infile = fopen(filename, "rb")) )
    {
        fprintf(stderr, "ERROR while opening input file %s for reading: %s\n",
                filename, strerror(errno));
        return;
    }

    if( !buff )
        buff = malloc(buffsize);

    /* 9B = normal length of header */
    if( fread( buff, 1, 9, infile ) != 9 )
    {
        fprintf(stderr, "ERROR reading header from input file %s: %s\n",
                filename, strerror(errno));
        return;
    }
    if( memcmp(buff, signature, 3) == 0 )
    {
        /* This file has a header; we'll do some brief checks and then skip it */
        size_t header_length, extra_length;

        if( memcmp(buff, signature, 3) != 0 )
        {
            fprintf(stderr, "ERROR: Signature bytes \"FLV\" not present at start of file\n");
            exit(1);
        }

        if( buff[3] != 1 )
            fprintf(stderr, "WARNING: FLV version %d detected (only tested with v. 1)\n", buff[3]);

        if( !(buff[4] & 4) )
            fprintf(stderr, "WARNING: No audio stream present in input file\n");

        if( !(buff[4] & 1) )
            fprintf(stderr, "WARNING: No video stream present in input file\n");

        header_length = (size_t)conv_ui32(&buff[5]);
        if( (header_length + 4) > buffsize ) /* Add 4 to include 1st back-pointer */
        {
            buffsize = header_length + 4;
            buff = realloc( buff, buffsize );
        }       
        extra_length = header_length - 9;
        fread( buff + 9, 1, extra_length + 4, infile );
    }
    else
        rewind(infile); /* It looks like the file contains raw FLV packets; rewind and start again. */

    while( !feof(infile) )
    {
        static struct FLVpacket packet;
        static size_t max_datasize = 0;
        size_t size;
        char key_frame = 0;

        /* Read the tag header (11 bytes) */
        size = fread( buff, 1, 11, infile );

        if( size == 0 )
        {
            if( !quiet )
                fprintf(stderr, "0 bytes read; stopping reading %s\n", filename);
            break;
        }

        /* FLV packet is structured as follows:
         * 1 byte packet type
         * 3 bytes datasize
         * 4 bytes timestamp
         * 3 bytes streamid
         * data payload (size specified previously)
	 *      (for a video packet, first nibble of payload indicates frame type;
	 *       however H.264 sequence headers are marked as if they were key 
	 *       frames - so first two bytes need to be checked to determine whether
	 *       or not the video packet contains a keyframe)
         * 4 bytes backpointer
         */
        packet.type = buff[0];       
        packet.datasize = conv_ui24(&buff[1], 0);
        packet.timestamp = conv_ui24(&buff[4], buff[7]);
        packet.streamid = conv_ui24(&buff[8], 0);
        if( packet.datasize > max_datasize )
        {
            max_datasize = packet.datasize;
            /* packet declared as static so packet.data will have been initialised to NULL */
            packet.data = realloc( packet.data, max_datasize );
        }
        fread( packet.data, 1, packet.datasize, infile );
        /* Read back-pointer */
        size = fread( buff, 1, 4, infile );
        /* backptr should equal the number of bytes in the whole packet including the payload and the
         * header; could use it as a sanity check if necessary */
        packet.backptr = conv_ui32(buff);

        if(packet.type == 18) /* Script data */
	{
            if(!metadata_extracted && !no_meta)
	    {
                /* Attempt to extract metadata from this packet */
	        metadata_extracted = extract_metadata(&packet);
	        if(!quiet && metadata_extracted)
	            fprintf(stderr, "Metadata successfully extracted.\n");
	    }
	    continue; /* Jump to next packet */
	}

        if(!seq_header_pkt.data && packet.type == 9 &&
	   (packet.data[0] & 0x0f) == 7 && packet.data[1] == 0) /* AVC sequence header */
	{
	    seq_header_pkt = packet;
            seq_header_pkt.data = malloc(packet.datasize);
            memcpy(seq_header_pkt.data, packet.data, packet.datasize);
	    continue; /* Jump to next packet */
	}

        if(packet.timestamp < mark_in ||             /* Before mark in point */
	   packet.timestamp >= mark_out ||           /* After mark out point */
           (packet.type != 8 && packet.type != 9))   /* Non video or audio packet */
	    continue; /* Jump to next packet */

        if( packet.type == 8 )
	    key_frame = 1; /* All audio packets are keyframes */
        else
	{
            unsigned char frame_type = (packet.data[0] & 0xf0) >> 4;
	    if(frame_type == 1)
	        key_frame = 1;
	}
        if(first_keyframe_timestamp == -1 && key_frame)
	    first_keyframe_timestamp = packet.timestamp;

        if( file_start_timestamp == -999999 )
        {
            if( packet.type == 9 ) /* Video packet */
            {
		if(key_frame)
	        {	
		    if(first_time)
		    {
                        /* First packet processed (either audio or video) is effectively the start of file */
			file_start_timestamp = -first_keyframe_timestamp;
		        first_time = 0;
		    }
		    else
                        /* Calculate starting timestamp based on video framerate */
                        file_start_timestamp = lastfile_video_timestamp + frame_interval - packet.timestamp;
		    if(!quiet)
		        fprintf(stderr, "%s: File start timestamp set to %ld (First video keyframe %d)\n",
				filename, file_start_timestamp, packet.timestamp);
                    buffer_packet( &packet, file_start_timestamp, 1); /* Flush buffer this time */
                }
	        /* Discard non-keyframe video packets received before first keyframe packet */
            }
            else
                /* Buffer packets until we get our first video keyframe that
		 * we can calculate the starting timestamp from */
                buffer_packet( &packet, -1, 0 );
        }
        else
            /* Write this packet to output stream */
            write_packet( &packet, file_start_timestamp );

    }

    if( !quiet )
        fprintf(stderr, "Closing %s\n", filename);
    if( fclose(infile) != 0 )
        fprintf(stderr, "ERROR while closing input file %s: %s\n",
                filename, strerror(errno));

    return;
}

/*
 * buffer_packet()
 * 
 * Add the FLV packet "packet" to an internal statically-held array of FLVpacket
 * structs. Duplicate the data payload and update the data pointer in the packet 
 * to point to the duplicated data.
 * If "flush" is non-zero, write out all the packets in the buffer using 
 * write_packet() in order received, free all the memory used for the 
 * payloads and reset the packet buffer count to 0.
 * "file_start_timestamp" should contain the timestamp for the start of the 
 * current file, and is passed to write_packet() when flushing the buffer.
 */
static void buffer_packet(struct FLVpacket *packet, long file_start_timestamp, char flush)
{
    static struct FLVpacket *pktarray = NULL;
    static int packets = 0, max_packets = 0;

    if( packets >= max_packets )
    {
        max_packets += 5;
        pktarray = realloc( pktarray, max_packets * sizeof(struct FLVpacket) );
    }

    pktarray[packets] = *packet;
    pktarray[packets].data = malloc(packet->datasize);
    memcpy(pktarray[packets].data, packet->data, packet->datasize);

    packets++;

    if(flush) /* Flush the buffer and free all data */
    {         /* Don't free the FLVpacket array as we may use it again */
        int i;

        for( i = 0; i < packets; i++ )
        {
	    if(seq_header_pkt.data && pktarray[i].type == 9)
	    {
	        /* Write sequence header immediately before first video packet */
	        seq_header_pkt.timestamp = pktarray[i].timestamp;
	        write_packet(&seq_header_pkt, file_start_timestamp);
	        free(seq_header_pkt.data);
	        seq_header_pkt.data = NULL;
	    }
            write_packet( &pktarray[i], file_start_timestamp );
            free(pktarray[i].data);
        }
        packets = 0;
    }

    return;   
}

/*
 * write_packet()
 * 
 * Writes the FLV packet described by the FLVpacket struct "packet" to the
 * output stream in the correct byte-stream format. The packet timestamp is
 * re-written on the fly after having "file_start_timestamp" added to it.
 * If the packet is an audio packet and the timestamp is less than or equal
 * to the value of "last_audio_timestamp", the packet is dropped and not
 * written.
 * 
 * If the packet being written is a video packet, global variable 
 * "last_video_timestamp" is updated to contain the value of the re-written
 * timestamp.
 * If the packet being written is an audio packet, global variable
 * "last_audio_timestamp" is updated to contain the value of the re-written
 * timestamp.
 */
static void write_packet(struct FLVpacket *packet, long file_start_timestamp)
{
    /* Calculate new timestamp */
    packet->timestamp += file_start_timestamp;
    /* Drop any overlapping audio packets */
    if( packet->type == 8 && (long)packet->timestamp <= last_audio_timestamp )
    {
	if(!quiet)
	    fprintf(stderr, "Dropping overlapping audio packet with timestamp %d; last audio packet at %d\n",
		    packet->timestamp, (unsigned int)last_audio_timestamp);
        return;
    }

    /* Write out first part of header (tag type and datasize) */
    write_output(&packet->type, 1);
    write_output(format_ui24(packet->datasize), 3);

    /* Write out timestamp ui24 value + top extension byte = 4 bytes to write out */
    write_output(format_ui24(packet->timestamp), 4);

    /* Write out streamid (should always be 0 anyway) */
    write_output(format_ui24(packet->streamid), 3);

    /* Write out data payload */
    write_output(packet->data, packet->datasize);

    /* Write out closing back pointer */
    write_output(format_ui32(packet->backptr), 4);

    if( packet->type == 9 ) /* Video packet */
        /* Update timestamp - used in calculating first timestamp for new file */
        last_video_timestamp = packet->timestamp;
    if( packet->type == 8 ) /* Audio packet */
        /* Update timestamp - used to ensure audio tracks don't overlap when joining files */
        last_audio_timestamp = packet->timestamp;
    last_packet_size = packet->datasize;

    return;   
}

/*
 * open_output()
 * 
 * Opens the file with pathname specified by global variable "filepath" and
 * stores the pointer to the resulting stream in global variable "outfile".
 * Opens in binary mode for appending - thus if the program is stopped and
 * started during the course of a minute, the old data will not be overwritten.
 * 
 * Prints an appropriate message to stderr and exits the program should an
 * error occur during opening the output.
 */
static void open_output(const char *mode)
{
    if( strcmp(filepath, "-") == 0 )
        outfile = stdout;    
    else if( !(outfile = fopen(filepath, mode)) )
    {
        fprintf(stderr, "ERROR while opening output file %s for writing: %s\n",
                filepath, strerror(errno));
        exit(1);
    }
    return;
}

/*
 * write_output()
 * 
 * Writes "bytes" bytes starting from the memory buffer at "buffer" to the
 * output stream specified by global variable "outfile".
 * 
 * Prints an appropriate message to stderr and exits the program should an
 * error or short count occur during writing.
 */
static void write_output(unsigned char *buffer, size_t bytes)
{
    if( fwrite(buffer, 1, bytes, outfile) != bytes )
    {
        fprintf(stderr, "ERROR while writing to output file %s: %s\n",
                filepath, strerror(errno));
        exit(1);
    }
    return;
}

/*
 * close_output()
 * 
 * Closes the output stream specified by global variable "outfile".
 * 
 * Prints an appropriate message to stderr and exits the program should an
 * error occur during closing.
 */
static void close_output(void)
{
    if( outfile != stdout && fclose(outfile) != 0 )
    {
        fprintf(stderr, "ERROR while closing output file %s: %s\n",
                filepath, strerror(errno));
        exit(1);
    }
    return;
}


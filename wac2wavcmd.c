//////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2014 Wildlife Acoustics, Inc.
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public LIcense as published by 
// the Free Software Foundation, either version 3 of the License, or 
// (at yuour option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//
// Wildlife Acoustics, Inc.
// 3 Clock Tower Place, Suite 210
// Maynard, MA 01754-2549
// +1 978 369 5225
// www.wildlifeacoustics.com
//
//////////////////////////////////////////////////////////////////////////////

// 
// wac2wavcmd.c VERSION 1.0
//
// This code will take an untriggered WAC file (version 4 or earlier) as 
// standard input and produce an uncompressed WAV file as standard output.
//
// This code serves as an example of how to decode the Wildlife Acoustics
// proprietary audio compression format known as "WAC".
//
// See comments to learn how GPS data, triggers, and recording tags may be
// interleaved in the data stream.
//
// The comments below are intended to provide some description of how the
// WAC format is implemented, but you should refer to the code as authoratative.
//
// A WAC file has the following format.  Note multi-byte values are 
// little-endian.
// 
//   1. WAC HEADER (24 bytes)
//      0x00 - 0x03 = "WAac" - identifies this as a WAC file
//      0x04        = Version number (<= 4)
//      0x05        = Channel count (1 for mono, 2 for stereo)
//      0x06 - 0x07 = Frame size = # samples per channel per frame
//      0x08 - 0x09 = Block size = # frames per block
//      0x0a - 0x0b = Flags
//                      0x0f = mask of "lossy bits".  For "WAC0", this is 0.
//                             For "WAC1", this is 1, and so on, representing
//                             increasing levels of compression as the number
//                             of discarded least-significant bits.  WAC0 is
//                             lossless compression, WAC1 is equivalent to 
//                             15-bit dynamic range, WAC2 is equivalent to
//                             14-bit dynamic range, and so on.
//                      0x10 = Triggered WAC file e.g. one or both channels
//                             have triggers.  What this means is that highly
//                             compressed zero-value frames may be inserted
//                             in the data stream representing untriggered
//                             time between triggered recordings.  Software
//                             capable of handling triggers can break a file
//                             into pieces discarding these zero-value frames.
//                             Note: This example code does not support
//                             this, but look in the code for "ZERO FRAME" 
//                             to see where this would be used.
//                      0x20 = GPS data present - GPS data is interleaved with 
//                             data in block headers.
//                      0x40 = TAG data present - TAG data is interleaved with
//                             data in block headers.  The TAG corresponds to
//                             an EM3/EM3+ button press to tag a recording.
//      0x0c - 0x0f = Sample rate (samples per second)
//      0x10 - 0x13 = Sample count (number of samples in WAC file per channel)
//      0x14 - 0x15 = Seek size (number of blocks per seek table entry)
//      0x16 - 0x17 = Seek entries (size of seek table in 32-bit words)
//
// 2. SEEK TABLE
//    The Seek Table contains (Seek entries) number of 4-byte (32-bit) 
//    values representing the absolute offset into the WAC file corresponding  
//    to each (Seek size) blocks.  The offset is measured in 16-bit words so
//    you would double these values to convert to a byte offset into the file.
//    The intention of the seek table is to make it easier to jump to a position
//    in the WAC file without needing to decompress all the data before that
//    position.  This code example does not use the seek table so we simply
//    skip over it.
//
// 3. BLOCKS OF FRAMES OF SAMPLES
//    Samples are grouped into frames (according to the frame size), and
//    frames are organized into blocks (according to the block size).
//    Additionally, blocks are organized into seek table entries as described
//    above according to the seek size.
//
//    Each block is aligned to a 16-bit boundary and consists of a block 
//    header followed by block size frames. The format of the block header is 
//    as follows:
//
//    0x00 - 0x03 = 0x00018000 = unique block header pattern
//    0x04 - 0x07 = block index (starting with zero and incrementing by one
//                  for each subsequent block used to keep things synchronized
//                  and detect file corruption.  This is also convenient for
//                  seeking to a particular block as the patterns here will
//                  not occur in the data stream.
//
//    Following the block header are a series of variable-length bit-fields
//    which do not necessarily line up on byte boundaries.  Refer to the 
//    ReadBits() function for specifics relating to the encoding.
//
//    If (flags & 0x20), then GPS data is present in every seek size blocks
//    beginning with the first block at index zero.  The GPS data is encoded as 
//    25-bits of signed latitude and 26-bits of signed longitude information.  
//    (using 2's complement notation). The latitude and longitude values in 
//    degrees can be determined by dividing these signed quantities by 100,000 
//    with positive values corresponding to North latitude and West longitude.
//
//    If (flags & 0x40), then tag data is present in every block and is 
//    represented by 4-bits.  For tagged recordings (e.g. from an EM3), the
//    tag values 1-4 correspond to the buttons 'A' through 'D', and a value 0
//    indicates that no tag is present.  While the tag button is pressed, 
//    blocks will be written with the corresponding tag.
//
//    Note that the GPS and TAG values are not implemented in this code and are
//    simply skipped, but please see the comments in the code for more 
//    information.
//
//    Following the block header and optional GPS or tag data are block size
//    frames of frame size samples for each channel.  For multi-channel 
//    recordings, samples are interleaved.
//
//    Compression uses Golumb coding of the deltas between successive samples.
//    The number of bits used to represent the remainder is variable and
//    optimized for each frame and for each channel.  The quotient is
//    represented by alternating 1/0 bits ahead of the remainder.
//
//    The frame begins with a 4-bit value for each channel indicating the
//    number of bits used to represent the remainder.  Note that a zero value
//    indicates that the frame contains no content e.g. representing the
//    space inbetween triggered recordings.
//
//    What follows are Golumb-encoded representations of deltas of interleaved 
//    (by channel) samples.  Details can be found in FrameDecode().
//
//    NOTE: We have not yet added Wildlife Acoustics metadata to the WAC
//    format and may do so in the future, quite likely by appending a 
//    "Wamd" chunk at the end of the file.  
//
//    NOTE: This code compiles on Linux and should be easy to port to other
//    applications.  Little-endian is assumed.
//
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct WacState_s
  {
    int            version;     // WAC file version number
    int            flags;       // WAC flags
    int            framesize;   // samples per channel per frame
    int            blocksize;   // frames per block
    int            seeksize;    // blocks per seek-table entry
    int            seekentries; // number of seek-table entries
    int            channelcount;// number of channels
    int            samplerate;  // sample rate
    unsigned long  samplecount; // number of samples in file per channel

    FILE          *filetbl[2];    // input and output file descriptors
    int            frameindex;    // current frame index
    int            filebit_index; // index to current bit in word (0-15)
    unsigned short bitbuffer;     // remaining bits in bit buffer

  } WacState;

// Forward declarations
int ReadBits(WacState *WP, int _bits);
unsigned short ReadWord(WacState *WP);
void FrameDecode(WacState *WP); 

// Macros for read/write 
#define READ(WP, buf, len) fread(buf, 1, len, (WP)->filetbl[0])
#define WRITE(WP, buf, len) fwrite(buf, 1, len, (WP)->filetbl[1])

// Simply take stdin to stdout
int main(int argc, char **argv)
{
  int i;
  unsigned ul;
  size_t sz;
  unsigned char hdr[24];
  unsigned char cc[4];

  // Copyright
  fprintf(stderr, "\r\nwac2wavcmd 1.0 Copyright (C) 2014 Wildlife Acoustics, Inc.\r\n\r\n"
                  "This program comes with ABSOLUTELY NO WARRANTY;\r\n"
		  "This is free software, and you are welcome to redistribute it\r\n" 
		  "under certain conditions;  Please refer to the GNU GPLv3 license at\r\n"
		  "<http://www.gnu.org/licenses/> for details.\r\n\r\n"
		  "Usage: reads stdin for .wac file and writes .wav file to stdout\r\n\r\n"
		  "Progress: "
         );

  // Initialize WAC state 
  WacState W;
  memset(&W, 0, sizeof(W));

  // For this simple example, we'll just read WAC from stdin and write WAV
  // to stdout...
  W.filetbl[0] = stdin;
  W.filetbl[1] = stdout;

  // Parse WAC header and validate supported formats
  if ((sz = READ(&W, hdr, 24)) != 24)
    {
      fprintf(stderr, "%s: Unexpected eof\n", argv[0]);
      exit(1);
    }

  // Verify "magic" header
  if (   hdr[0] != 'W'
      || hdr[1] != 'A'
      || hdr[2] != 'a'
      || hdr[3] != 'c'
     )
    {
      fprintf(stderr, "%s: Input not a WAC file\n", argv[0]);
      exit(1);
    }

  // Check version
  W.version = hdr[4];
  if (W.version > 4)
    {
      fprintf(stderr, "%s: Input version %d not supported\n", argv[0], W.version);
      exit(1);
    }

  // Read channel count and frame size
  W.channelcount = hdr[5];
  W.framesize = hdr[6] | (hdr[7] << 8);

  // All Wildlife Acoustics WAC files have 512-byte (256-sample mono or
  // 128 sample stereo) frames.
  if (W.channelcount * W.framesize != 256)
    {
      fprintf(stderr, "%s: Unsupported block size %d\n", argv[0],W.channelcount*W.framesize);
      exit(1);
    }

  // All Wildlife Acoustics WAC files have 1 or 2 channels
  if (W.channelcount > 2)
    {
      fprintf(stderr, "%s: Unsupported channel count %d\n", argv[0], W.channelcount);
      exit(1);
    }

  // Read flags
  W.flags = hdr[10] | (hdr[11] << 8);

  // For this example, we do not support triggered WAC files because we are
  // simply streaming to a single WAV file.  See FrameDecode() logic below
  // about triggered WAC mode.  If any special "zero frames" are present, this
  // bit will be set.  For non-triggered recordings, this bit will be clear.
  if (W.flags & 0x10)
    {
      fprintf(stderr, "%s: Triggered WAC files not supported\n", argv[0]);
      exit(1);
    }

  // Parse additional fields from the WAC header
  W.blocksize = hdr[8] | (hdr[9] << 8);
  W.samplerate = hdr[12] | (hdr[13] << 8) | (hdr[14] << 16) | (hdr[15] << 24);
  W.samplecount = hdr[16] | (hdr[17] << 8) | (hdr[18] << 16) | (hdr[19] << 24);
  W.seeksize = hdr[20] | (hdr[21] << 8);
  W.seekentries = hdr[22] | (hdr[23] << 8);

  // Skip over the seek table (not used in this example)
  for (i = 0; i < W.seekentries; i++)
    {
      if ((sz = READ(&W, hdr, 4)) != 4)
        {
	  fprintf(stderr, "%s: Unexpected EOF\n", argv[0]);
	  exit(1);
	}
    }

  // Write WAV file header from WAC header information
  WRITE(&W, "RIFF", 4);
  ul =   4 // "WAVE
       + 8 + 16 // fmt chunk
       + 8      // data chunk
       + 2 * W.samplecount * W.channelcount;
  cc[3] = (ul >> 24) & 0xff;
  cc[2] = (ul >> 16) & 0xff;
  cc[1] = (ul >>  8) & 0xff;
  cc[0] = (ul      ) & 0xff;
  WRITE(&W, cc, 4);
  WRITE(&W, "WAVE", 4);
  WRITE(&W, "fmt ", 4);
  ul = 16;
  cc[3] = (ul >> 24) & 0xff;
  cc[2] = (ul >> 16) & 0xff;
  cc[1] = (ul >>  8) & 0xff;
  cc[0] = (ul      ) & 0xff;
  WRITE(&W, cc, 4);
  cc[0] = 1; // tag
  cc[1] = 0; 
  WRITE(&W, cc, 2);
  cc[0] = W.channelcount; 
  WRITE(&W, cc, 2);
  ul = W.samplerate;
  cc[3] = (ul >> 24) & 0xff;
  cc[2] = (ul >> 16) & 0xff;
  cc[1] = (ul >>  8) & 0xff;
  cc[0] = (ul      ) & 0xff;
  WRITE(&W, cc, 4);
  ul *= W.channelcount * 2; // bytes per second
  cc[3] = (ul >> 24) & 0xff;
  cc[2] = (ul >> 16) & 0xff;
  cc[1] = (ul >>  8) & 0xff;
  cc[0] = (ul      ) & 0xff;
  WRITE(&W, cc, 4);
  cc[0] = 2*W.channelcount; // bytes per sample
  cc[1] = 0;
  WRITE(&W, cc, 2);
  cc[0] = 16; // bits per sample
  WRITE(&W, cc, 2);
  WRITE(&W, "data", 4); 
  ul = W.samplecount * W.channelcount * 2;
  cc[3] = (ul >> 24) & 0xff;
  cc[2] = (ul >> 16) & 0xff;
  cc[1] = (ul >>  8) & 0xff;
  cc[0] = (ul      ) & 0xff;
  WRITE(&W, cc, 4);

  // Read frames of data and WRITE samples out
  while (W.samplecount > 0)
    {
      FrameDecode(&W);
      W.samplecount -= W.framesize;
    }

  // All done
  fprintf(stderr, "\r\n");
  exit(0);
}

// Read bits:
//
// This function reads the specified number of bits from the file (1-16) and
// returns the signed value.
//
// We buffer 16-bits at a time in bitbuffer and keep track of the current 
// filebit_index number of bits remaining in the buffer.
//
int ReadBits(WacState *WP, int bits)
{
  unsigned long x = 0;

  // While we need bits...
  while (bits > 0)
    {
      // If starting a new 16-bit word, read the next 16 bits
      if (WP->filebit_index == 0)
        {
	  READ(WP, &WP->bitbuffer, 2);
	  WP->filebit_index = 16;
	}

      // If all the bits we need are in the current word, extract the bits,
      // update state, and break out of the loop.
      if (bits < WP->filebit_index)
        {
	  x <<= bits;
	  x |= WP->bitbuffer >> (16 - bits);
	  WP->bitbuffer <<= bits;
	  WP->filebit_index -= bits;
	  break;
	}
      else
        {
	  // Otherwise extract the bits we have and continue (which will
	  // then load the next 16-bits into the bitbuffer
	  x <<= WP->filebit_index;
	  x |= WP->bitbuffer >> (16 - WP->filebit_index);
	  bits -= WP->filebit_index;
	  WP->filebit_index = 0;
	}
    }
  return (int) x;
}

// ReadWord
//
// Skip to 16-bit boundary and read and return the next 16 bits.
//
unsigned short ReadWord(WacState *WP)
{
  WP->filebit_index = 0;
  return ReadBits(WP, 16);
}

// FrameDecode
//
// Decode the next frame and write interleaved 16-bit samples to output file
//
void FrameDecode(WacState *WP)
{
  unsigned short s;
  int i;
  int ch;
  unsigned short code;
  short lastsample[2];
  int g[2];
  int lossybits = WP->flags & 0x0f;

  // At start of block parse block header
  if (0 == (WP->frameindex % WP->blocksize))
    {
      // Verify that the block header is valid and as expected
      int block = WP->frameindex / WP->blocksize;
      if (   ReadWord(WP) != 0x8000
          || ReadWord(WP) != 0x0001
	  || ReadWord(WP) != (block & 0xffff)
	  || ReadWord(WP) != ((block >> 16) & 0xffff)
	 )
	{
	  fprintf(stderr, "Bad block header\n");
	  exit(1);
	}

      if (0 == WP->frameindex % 1024)
        {
          fprintf(stderr, "."); // progress...
	}

      // If GPS data present and the block number is modulo the blocks per
      // seek table entry, then load the latitude and longitude.
      if ((WP->flags & 0x20) && 0 == (block % WP->seeksize))
        {
	  int lathi = ReadBits(WP,9);
	  int latlo = ReadBits(WP,16);
	  int lonhi = ReadBits(WP,10);
	  int lonlo = ReadBits(WP,16);

	  // For this example, we are not actually using the latitude and
	  // longitude.  But these values are 100,000 times the latitude and
	  // longitude in degrees with positive sign indicating north latitude
	  // and west longitude.  These can be derived as follows:
	  //
	  //   float lat = ((lathi<<16)|latlo) / 100000.0;
	  //   float lon = ((lonhi<<16)|lonlo) / 100000.0;
	}

      // If tag data present read it.  For this example, we aren't using this
      // data.  But for EM3 recordings, for example, this tag value would
      // be 1-4 corresponding to buttons A-D being depressed during this frame.
      if (WP->flags & 0x40)
        {
	  int tag = ReadBits(WP,4);
	}
    }
  // Advance frame 
  WP->frameindex++;

  // Read the per-channel Golumb remainder code size
  for (ch = 0; ch < WP->channelcount; ch++)
    {
      lastsample[ch] = 0;
      g[ch] = ReadBits(WP,4);
    }

  // Read samples for frame
  for (i = 0; i < WP->framesize; i++)
    {
      // Interleave channels
      for (ch = 0; ch < WP->channelcount; ch++)
        {
	  int delta;
	  int stopbit;

	  // ZERO FRAME
	  // Special case: For triggered WAC files, the code size is set to
	  // zero to indicate a zero-value frame.  This represents the
	  // space between triggered events in the WAC file.  A trigger-aware
	  // parser would look for onset/offset of this condition to break up
	  // a WAC file into individual triggers.  In this example, we are just
	  // filling the space with zero sample values.  And we won't actually
	  // get to this code because the presence of these special zero-value
	  // frames also requires that the header flags has 0x10 set.  In
	  // main() above, we exit with an error if this is the case.
	  if (g[ch] == 0)
	    {
	      s = 0;
	      WRITE(WP, &s, 2);
	      continue;
	    }

	  // Read the remainder code from the specified number of bits
	  code = ReadBits(WP,g[ch]);

	  // Read the quotient represented by alternating 1/0 pattern 
	  // following the remainder code
	  stopbit = (code & 1) ^ 1;
	  while (stopbit != ReadBits(WP,1))
	    {
	      code += 1 << g[ch];
	      stopbit ^= 1;
	    }

	  // Adjust for sign
	  if (code & 1)
	    {
	      delta = -(code+1)/2;
	    }
	  else
	    {
	      delta = code/2;
	    }

	  // Compute sample value as delta from previous sample
	  delta += lastsample[ch];
	  lastsample[ch] = delta;

	  // Restore dropped least-significant bits used in higher levels
	  // of compression e.g. WAC1, WAC2, etc.
	  delta <<= lossybits;

	  // Write 16-bit sample to output file
	  s = delta;
	  WRITE(WP, &s, 2);
	}
    }
}

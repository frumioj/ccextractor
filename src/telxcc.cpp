/*!
(c) 2011-2012 Petr Kutalek, Forers, s. r. o.: telxcc

Some portions/inspirations:
(c) 2007 Vincent Penne, telx.c : Minimalistic Teletext subtitles decoder
(c) 2001-2005 by dvb.matt, ProjectX java dvb decoder
(c) Dave Chapman <dave@dchapman.com> 2003-2004, dvbtextsubs
(c) Ralph Metzler, DVB driver, vbidecode
(c) Jan Pantelje, submux-dvd
(c) Ragnar Sundblad, dvbtextsubs, VDR teletext subtitles plugin
(c) Scott T. Smith, dvdauthor
(c) 2007 Vladimir Voroshilov <voroshil@gmail.com>, mplayer
(c) 2001, 2002, 2003, 2004, 2007 Michael H. Schimek, libzvbi -- Error correction functions

Code contribution:
Laurent Debacker (https://github.com/debackerl)

Windows App Icon:
(c) Oxygen Team, http://www.oxygen-icons.org


This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.


I would like to thank:
David Liontooth <lionteeth@cogweb.net> for providing me with Swedish and Norwegian TS samples and patient testing
Professor Francis F Steen and his team from UCLA for contribution
Laurent Debacker (https://github.com/debackerl) for bug fixes
Philip Klenk <philip.klenk@web.de> for providing me with German TS sample and contribution


telxcc conforms to ETSI 300 706 Presentation Level 1.5: Presentation Level 1 defines the basic Teletext page,
characterised by the use of spacing attributes only and a limited alphanumeric and mosaics repertoire.
Presentation Level 1.5 decoder responds as Level 1 but the character repertoire is extended via packets X/26.

Further documentation:
ETSI TS 101 154 V1.9.1 (2009-09), Technical Specification
  Digital Video Broadcasting (DVB); Specification for the use of Video and Audio Coding in Broadcasting Applications based on the MPEG-2 Transport Stream
ETSI EN 300 231 V1.3.1 (2003-04), European Standard (Telecommunications series)
  Television systems; Specification of the domestic video Programme Delivery Control system (PDC)
ETSI EN 300 472 V1.3.1 (2003-05), European Standard (Telecommunications series)
  Digital Video Broadcasting (DVB); Specification for conveying ITU-R System B Teletext in DVB bitstreams
ETSI EN 301 775 V1.2.1 (2003-05), European Standard (Telecommunications series)
  Digital Video Broadcasting (DVB); Specification for the carriage of Vertical Blanking Information (VBI) data in DVB bitstreams
ETS 300 706 (May 1997)
  Enhanced Teletext Specification
ETS 300 708 (March 1997)
  Television systems; Data transmission within Teletext
ISO/IEC STANDARD 13818-1 Second edition (2000-12-01)
  Information technology — Generic coding of moving pictures and associated audio information: Systems
ISO/IEC STANDARD 6937 Third edition (2001-12-15)
  Information technology — Coded graphic character set for text communication — Latin alphabet
Werner Brückner -- Teletext in digital television
*/

#include "ccextractor.h"
#include "hamming.h"
#include "teletext.h"
#include <signal.h>
#define TELXCC_VERSION "2.2.0"

// switch STDIN and all normal files into binary mode -- needed for Windows
#ifdef __MINGW32__
#include <fcntl.h>
int _CRT_fmode = _O_BINARY;
int _fmode = _O_BINARY;
#endif

// for better UX in Windows we want to detect that app is not run by "double-clicking" in Explorer
#ifdef __MINGW32__
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0502
#define _WIN32_IE 0x0400
#include <windows.h>
#include <commctrl.h>
#endif

typedef enum {
	YES = 1,
	NO = 0,
	UNDEF = 255
} bool_t;

// size of a TS packet in bytes
#define TS_PACKET_SIZE 188

// size of a TS packet payload in bytes
#define TS_PACKET_PAYLOAD_SIZE 184

// size of a packet payload buffer
#define PAYLOAD_BUFFER_SIZE 4096

/*
// number of TS packets to be analyzed while searching for PMTs
#define TS_ANALYSIS_LENGTH 4096
*/

typedef struct {
	uint8_t sync : 8;
	uint8_t transport_error : 1;
	uint8_t payload_unit_start : 1;
	uint8_t transport_priority : 1;
	uint16_t pid : 13;
	uint8_t scrambling_control : 2;
	uint8_t adaptation_field_exists : 2;
	uint8_t continuity_counter : 4;
} ts_packet_t;

typedef struct {
	uint16_t program_num : 16;
	uint16_t program_pid : 13;
} pat_section_t;

typedef struct {
	uint8_t pointer_field : 8;
	uint8_t table_id : 8; // 0x00
	uint16_t section_length : 10;
	uint8_t current_next_indicator : 1;
} pat_t;

typedef struct {
	uint8_t stream_type : 8;
	uint16_t elementary_pid : 13;
	uint16_t es_info_length : 10;
} pmt_program_descriptor_t;

/*
typedef struct {
	uint8_t descriptor_tag : 8;
	uint8_t descriptor_length : 8;
} pmt_teletext_descriptor_t;

typedef struct {
	uint32_t iso_639_language_code : 24;
	uint8_t teletext_type : 5;
	uint8_t teletext_magazine_number : 4;
	uint8_t teletext_page_number : 8;
} pmt_teletext_descriptor_payload_t;
*/

typedef struct {
	uint8_t pointer_field : 8;
	uint8_t table_id : 8; // 0x02
	uint16_t section_length : 10;
	uint16_t program_num : 16;
	uint8_t current_next_indicator : 1;
	uint16_t pcr_pid : 16;
	uint16_t program_info_length : 10;
} pmt_t;

/*
typedef enum {
	TELETEXT_DESCRIPTOR_TELETEXT_TYPE_RESERVED = 0,
	TELETEXT_DESCRIPTOR_TELETEXT_TYPE_INITIAL = 1,
	TELETEXT_DESCRIPTOR_TELETEXT_TYPE_SUBTITLE = 2,
	TELETEXT_DESCRIPTOR_TELETEXT_TYPE_ADDITIONAL_INFO = 3,
	TELETEXT_DESCRIPTOR_TELETEXT_TYPE_PROGRAM_SCHEDULE = 4,
	TELETEXT_DESCRIPTOR_TELETEXT_TYPE_SUBTITILE_CC = 5
} teletext_descriptor_teletext_type_t;
*/

typedef enum {
	DATA_UNIT_EBU_TELETEXT_NONSUBTITLE = 0x02,
	DATA_UNIT_EBU_TELETEXT_SUBTITLE = 0x03,
	DATA_UNIT_EBU_TELETEXT_INVERTED = 0x0c,
	DATA_UNIT_VPS = 0xc3,
	DATA_UNIT_CLOSED_CAPTIONS = 0xc5
} data_unit_t;

typedef enum {
	TRANSMISSION_MODE_PARALLEL = 0,
	TRANSMISSION_MODE_SERIAL = 1
} transmission_mode_t;

const char* TTXT_COLOURS[8] = {
//  black,     red,       green,     yellow,    blue,      magenta,   cyan,      white
	"#000000", "#ff0000", "#00ff00", "#ffff00", "#0000ff", "#ff00ff", "#00ffff", "#ffffff"
};

// 1-byte alignment; just to be sure, this struct is being used for explicit type conversion
// FIXME: remove explicit type conversion from buffer to structs
#pragma pack(push)
#pragma pack(1)
typedef struct {
	uint8_t : 8; // clock run in
	uint8_t : 8; // framing code, not needed, ETSI 300 706: const 0xe4
	uint8_t address[2];
	uint8_t data[40];
} teletext_packet_payload_t;
#pragma pack(pop)

typedef struct {
	uint64_t show_timestamp; // show at timestamp (in ms)
	uint64_t hide_timestamp; // hide at timestamp (in ms)
	uint16_t text[25][40]; // 25 lines x 40 cols (1 screen/page) of wide chars
	uint8_t tainted : 1; // 1 = text variable contains any data
} teletext_page_t;

// application config global variable
struct s_teletext_config tlt_config = { 0 };

// macro -- output only when increased verbosity was turned on
#define VERBOSE_ONLY if (tlt_config.verbose == YES)

// application states -- flags for notices that should be printed only once
struct s_states {
	uint8_t x28_not_implemented_notified : 1;
	uint8_t m29_not_implemented_notified : 1;
	uint8_t programme_info_processed : 1;
	uint8_t pts_initialized : 1;
} states = { 0 };

// SRT frames produced
uint32_t tlt_frames_produced = 0;

// subtitle type pages bitmap, 2048 bits = 2048 possible pages in teletext (excl. subpages)
uint8_t cc_map[256] = { 0 };

// global TS PCR value
uint32_t global_timestamp = 0;

// last timestamp computed
uint64_t last_timestamp = 0;

// working teletext page buffer
teletext_page_t page_buffer = { 0 };

// teletext transmission mode
transmission_mode_t transmission_mode = TRANSMISSION_MODE_SERIAL;

// flag indicating if incoming data should be processed or ignored
uint8_t receiving_data = NO;

// current charset (charset can be -- an very often is -- changed during transmission)
uint8_t current_charset = 0;

// PMTs table
#define TS_PMT_MAP_SIZE 128
uint16_t pmt_map[TS_PMT_MAP_SIZE] = { 0 };
uint16_t pmt_map_length = 0;

// TTXT streams table
#define TS_PMT_TTXT_MAP_SIZE 128
uint16_t pmt_ttxt_map[TS_PMT_MAP_SIZE] = { 0 };
uint16_t pmt_ttxt_map_length = 0;

// FYI, packet counter
uint32_t tlt_packet_counter = 0;

int telxcc_inited=0;

inline bool_t in_array(uint16_t *array, uint16_t length, uint16_t element) {
	bool_t r = NO;
	for (uint16_t i = 0; i < length; i++)
		if (array[i] == element) {
			r = YES;
			break;
		}
	return r;
}

// extracts magazine No from teletext page
#define MAGAZINE(p) ((p >> 8) & 0xf)

// extracts page No from teletext page
#define PAGE(p) (p & 0xff)

// ETS 300 706, chapter 8.2
uint8_t unham_8_4(uint8_t a) {
	uint8_t r = UNHAM_8_4[a];
	if (r == 0xff) {
		dbg_print (DMT_TELETEXT, "- Unrecoverable data error; UNHAM8/4(%02x)\n", a);
	}
	return (r & 0x0f);
}

// ETS 300 706, chapter 8.3
uint32_t unham_24_18(uint32_t a) {
	uint8_t B0 = a & 0xff;
	uint8_t B1 = (a >> 8) & 0xff;
	uint8_t B2 = (a >> 16) & 0xff;

	uint8_t D1_D4 = UNHAM_24_18_D1_D4[B0 >> 2];
	uint8_t D5_D11 = B1 & 0x7f;
	uint8_t D12_D18 = B2 & 0x7f;

	uint32_t d = D1_D4 | (D5_D11 << 4) | (D12_D18 << 11);
	uint8_t ABCDEF = UNHAM_24_18_PAR[0][B0] ^ UNHAM_24_18_PAR[1][B1] ^ UNHAM_24_18_PAR[2][B2];
	uint32_t r = d ^ UNHAM_24_18_ERR[ABCDEF];

	return r;
}

void timestamp_to_srttime(uint64_t timestamp, char *buffer) {
	uint64_t p = timestamp;
	uint8_t h = (uint8_t) (p / 3600000);
	uint8_t m = (uint8_t) (p / 60000 - 60 * h);
	uint8_t s = (uint8_t) (p / 1000 - 3600 * h - 60 * m);
	uint16_t u = (uint16_t) (p - 3600000 * h - 60000 * m - 1000 * s);
	sprintf(buffer, "%02"PRIu8":%02"PRIu8":%02"PRIu8",%03"PRIu16, h, m, s, u);
}

// wide char (16 bits) to utf-8 conversion
void ucs2_to_utf8(char *r, uint16_t ch) {
	if (ch < 0x80) {
		r[0] = ch & 0x7f;
		r[1] = 0;
		r[2] = 0;
		r[3] = 0;
	}
	else if (ch < 0x800) {
		r[0] = (ch >> 6) | 0xc0;
		r[1] = (ch & 0x3f) | 0x80;
		r[2] = 0;
		r[3] = 0;
	}
	else {
		r[0] = (ch >> 12) | 0xe0;
		r[1] = ((ch >> 6) & 0x3f) | 0x80;
		r[2] = (ch & 0x3f) | 0x80;
		r[3] = 0;
	}
}

// check parity and translate any reasonable teletext character into ucs2
uint16_t telx_to_ucs2(uint8_t c) {
	if (PARITY_8[c] == 0) {
		dbg_print (DMT_TELETEXT,  "- Unrecoverable data error; PARITY(%02x)\n", c);
		return 0x20;
	}

	uint16_t r = c & 0x7f;
	if (r >= 0x20) r = G0[LATIN][r - 0x20];
	return r;
}

void process_page(teletext_page_t *page) {
#ifdef DEBUG
	for (uint8_t row = 1; row < 25; row++) {
		fprintf(stdout, "DEBUG[%02u]: ", row);
		for (uint8_t col = 0; col < 40; col++) fprintf(stdout, "%3x ", page->text[row][col]);
		fprintf(stdout, "\n");
	}
	fprintf(stdout, "\n");
#endif

	// optimization: slicing column by column -- higher probability we could find boxed area start mark sooner
	uint8_t page_is_empty = YES;
	for (uint8_t col = 0; col < 40; col++)
		for (uint8_t row = 1; row < 25; row++)
			if (page->text[row][col] == 0x0b) {
				page_is_empty = NO;
				goto page_is_empty;
			}
	page_is_empty:
	if (page_is_empty == YES) return;

	if (page->show_timestamp > page->hide_timestamp) page->hide_timestamp = page->show_timestamp;

	char timecode_show[24] = { 0 };
	timestamp_to_srttime(page->show_timestamp, timecode_show);
	timecode_show[12] = 0;

	char timecode_hide[24] = { 0 };
	int time_reported=0;

	timestamp_to_srttime(page->hide_timestamp, timecode_hide);
	timecode_hide[12] = 0;

	// print SRT frame
	fdprintf(wbout1.fh, "%"PRIu32"\r\n%s --> %s\r\n", ++tlt_frames_produced, timecode_show, timecode_hide);

	// Also update GUI...
	if (gui_mode_reports)
	{
		fprintf (stderr, "###SUBTITLE#");
		char timecode_show_mmss[6], timecode_hide_mmss[6];
		memcpy (timecode_show_mmss, timecode_show+3, 5);
		memcpy (timecode_hide_mmss, timecode_hide+3, 5);
		timecode_show_mmss[5]=0; 
		timecode_hide_mmss[5]=0;
		// Note, only MM:SS here as we need to save space in the preview window
		fprintf (stderr, "%s#%s#",
			timecode_show_mmss, timecode_hide_mmss);
		time_reported=1;
	}

	// process data
	for (uint8_t row = 1; row < 25; row++) {
		// anchors for string trimming purpose
		uint8_t col_start = 40;
		uint8_t col_stop = 40;

		for (int8_t col = 39; col >= 0; col--)
			if (page->text[row][col] == 0xb) {
				col_start = col;
				break;
			}
		// line is empty
		if (col_start > 39) continue;

		for (uint8_t col = col_start + 1; col <= 39; col++) {
			if (page->text[row][col] > 0x20) {
				if (col_stop > 39) col_start = col;
				col_stop = col;
			}
			if (page->text[row][col] == 0xa) break;
		}
		// line is empty
		if (col_stop > 39) continue;

		// ETS 300 706, chapter 12.2: Alpha White ("Set-After") - Start-of-row default condition.
		// used for colour changes _before_ start box mark
		// white is default as stated in ETS 300 706, chapter 12.2
		// black(0), red(1), green(2), yellow(3), blue(4), magenta(5), cyan(6), white(7)
		uint8_t foreground_color = 0x7;
		uint8_t font_tag_opened = NO;

		if (!time_reported && gui_mode_reports)
			fprintf (stderr, "###SUBTITLE###");					

		for (uint8_t col = 0; col <= col_stop; col++) {
			// v is just a shortcut
			uint16_t v = page->text[row][col];

			if (col < col_start) {
				if (v <= 0x7) foreground_color = (uint8_t) v;
			}

			if (col == col_start) {
				if ((foreground_color != 0x7) && !nofontcolor) {
					fdprintf(wbout1.fh, "<font color=\"%s\">", TTXT_COLOURS[foreground_color]);
					font_tag_opened = YES;
				}
			}

			if (col >= col_start) {
				if (v <= 0x7) {
					// ETS 300 706, chapter 12.2: Unless operating in "Hold Mosaics" mode,
					// each character space occupied by a spacing attribute is displayed as a SPACE.
					if (!nofontcolor) {
						if (font_tag_opened == YES) {
							fdprintf(wbout1.fh, "</font> ");
							font_tag_opened = NO;
						}

						// black is considered as white for telxcc purpose
						// telxcc writes <font/> tags only when needed
						if ((v > 0x0) && (v < 0x7)) {
							fdprintf(wbout1.fh, "<font color=\"%s\">", TTXT_COLOURS[v]);
							font_tag_opened = YES;
						}
					}
					else v = 0x20;
				}

				if (v >= 0x20) {
					char u[4] = {0, 0, 0, 0};
					ucs2_to_utf8(u, v);
					fdprintf(wbout1.fh, "%s", u);
					if (gui_mode_reports && u[1]==0) // For now we just handle the easy stuff
						fprintf (stderr,"%c",u[0]);
				}
			}
		}

		if ((!nofontcolor) && (font_tag_opened == YES)) {
			fdprintf(wbout1.fh, "</font>");
			font_tag_opened = NO;
		}

		fdprintf(wbout1.fh, "\r\n");
		if (gui_mode_reports)
		{
			fprintf (stderr,"\n");			
			time_reported=0;
		}
	}

	fdprintf(wbout1.fh, "\r\n");	
	if (gui_mode_reports)
		fflush (stderr);
}

void process_telx_packet(data_unit_t data_unit_id, teletext_packet_payload_t *packet, uint64_t timestamp) {
	// variable names conform to ETS 300 706, chapter 7.1.2
	uint8_t address = (unham_8_4(packet->address[1]) << 4) | unham_8_4(packet->address[0]);
	uint8_t m = address & 0x7;
	if (m == 0) m = 8;
	uint8_t y = (address >> 3) & 0x1f;

	if (y == 0) {
		// CC map
		uint8_t i = (unham_8_4(packet->data[1]) << 4) | unham_8_4(packet->data[0]);
		uint8_t flag_subtitle = (unham_8_4(packet->data[5]) & 0x08) >> 3;
		cc_map[i] |= flag_subtitle << (m - 1);

		if ((tlt_config.page == 0) && (flag_subtitle == YES) && (i < 0xff)) {
			tlt_config.page = (m << 8) | (unham_8_4(packet->data[1]) << 4) | unham_8_4(packet->data[0]);
			fprintf(stderr, "- No teletext page specified, first received suitable page is %03x, not guaranteed\n", tlt_config.page);
		}

	 	// Page number and control bits
		uint16_t page_number = (m << 8) | (unham_8_4(packet->data[1]) << 4) | unham_8_4(packet->data[0]);
		uint8_t charset = ((unham_8_4(packet->data[7]) & 0x08) | (unham_8_4(packet->data[7]) & 0x04) | (unham_8_4(packet->data[7]) & 0x02)) >> 1;
		uint8_t flag_suppress_header = unham_8_4(packet->data[6]) & 0x01;
		//uint8_t flag_inhibit_display = (unham_8_4(packet->data[6]) & 0x08) >> 3;

		// ETS 300 706, chapter 9.3.1.3:
		// When set to '1' the service is designated to be in Serial mode and the transmission of a page is terminated
		// by the next page header with a different page number.
		// When set to '0' the service is designated to be in Parallel mode and the transmission of a page is terminated
		// by the next page header with a different page number but the same magazine number.
		// The same setting shall be used for all page headers in the service.
		// ETS 300 706, chapter 7.2.1: Page is terminated by and excludes the next page header packet
		// having the same magazine address in parallel transmission mode, or any magazine address in serial transmission mode.
		transmission_mode = (transmission_mode_t) (unham_8_4(packet->data[7]) & 0x01);

		// FIXME: Well, this is not ETS 300 706 kosher, however we are interested in DATA_UNIT_EBU_TELETEXT_SUBTITLE only
		if ((transmission_mode == TRANSMISSION_MODE_PARALLEL) && (data_unit_id != DATA_UNIT_EBU_TELETEXT_SUBTITLE)) return;

		if ((receiving_data == YES) && (
				((transmission_mode == TRANSMISSION_MODE_SERIAL) && (PAGE(page_number) != PAGE(tlt_config.page))) ||
				((transmission_mode == TRANSMISSION_MODE_PARALLEL) && (PAGE(page_number) != PAGE(tlt_config.page)) && (m == MAGAZINE(tlt_config.page)))
			)) {
			receiving_data = NO;
			return;
		}

		// Page transmission is terminated, however now we are waiting for our new page
		if (page_number != tlt_config.page) return;

		// Now we have the begining of page transmission; if there is page_buffer pending, process it
		if (page_buffer.tainted == YES) {
			// it would be nice, if subtitle hides on previous video frame, so we contract 40 ms (1 frame @25 fps)
			page_buffer.hide_timestamp = timestamp - 40;
			process_page(&page_buffer);
		}

		page_buffer.show_timestamp = timestamp;
		page_buffer.hide_timestamp = 0;
		memset(page_buffer.text, 0x00, sizeof(page_buffer.text));
		page_buffer.tainted = NO;
		receiving_data = YES;

		// remap current Latin G0 chars
		// TODO: refactore
		if (charset != current_charset) {
			G0[LATIN][0x23 - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][ 0];
			G0[LATIN][0x24 - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][ 1];
			G0[LATIN][0x40 - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][ 2];
			G0[LATIN][0x5b - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][ 3];
			G0[LATIN][0x5c - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][ 4];
			G0[LATIN][0x5d - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][ 5];
			G0[LATIN][0x5e - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][ 6];
			G0[LATIN][0x5f - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][ 7];
			G0[LATIN][0x60 - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][ 8];
			G0[LATIN][0x7b - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][ 9];
			G0[LATIN][0x7c - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][10];
			G0[LATIN][0x7d - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][11];
			G0[LATIN][0x7e - 0x20] = G0_LATIN_NATIONAL_SUBSETS[charset][12];
			current_charset = charset;
			dbg_print (DMT_TELETEXT, "- G0 Charset translation table remapped to G0 Latin National Subset ID %1x\n", current_charset);
		}

		// I know -- not needed; in subtitles we will never need disturbing teletext page status bar
		// displaying tv station name, current time etc.
		if (flag_suppress_header == NO) {
			for (uint8_t i = 14; i < 40; i++) page_buffer.text[y][i] = telx_to_ucs2(packet->data[i]);
			//page_buffer.tainted = YES;
		}
	}
	else if ((y >= 1) && (y <= 23) && (m == MAGAZINE(tlt_config.page)) && (receiving_data == YES)) {
		// ETS 300 706, chapter 9.4.1: Packets X/26 at presentation Levels 1.5, 2.5, 3.5 are used for addressing
		// a character location and overwriting the existing character defined on the Level 1 page
		// ETS 300 706, annex B.2.2: Packets with Y = 26 shall be transmitted before any packets with Y = 1 to Y = 25;
		// so page_buffer.text[y][i] may already contain any character received
		// in frame number 26, skip original G0 character
		for (uint8_t i = 0; i < 40; i++) if (page_buffer.text[y][i] == 0x00) page_buffer.text[y][i] = telx_to_ucs2(packet->data[i]);
		page_buffer.tainted = YES;
	}
	else if ((y == 26) && (m == MAGAZINE(tlt_config.page)) && (receiving_data == YES)) {
		// ETS 300 706, chapter 12.3.2 (X/26 definition)
		uint8_t x26_row = 0;
		uint8_t x26_col = 0;

		uint32_t decoded[13] = { 0 };
		for (uint8_t i = 1, j = 0; i < 40; i += 3, j++) decoded[j] = unham_24_18((packet->data[i + 2] << 16) | (packet->data[i + 1] << 8) | packet->data[i]);

		for (uint8_t j = 0; j < 13; j++) {
			// invalid data (HAM24/18 uncorrectable error detected), skip group
			if ((decoded[j] & 0x80000000) > 0) {
				dbg_print (DMT_TELETEXT, "- Unrecoverable data error; UNHAM24/18()=%04x\n", decoded[j]);
				continue;
			}

			uint8_t data = (decoded[j] & 0x3f800) >> 11;
			uint8_t mode = (decoded[j] & 0x7c0) >> 6;
			uint8_t address = decoded[j] & 0x3f;
			uint8_t row_address_group = (address >= 40) && (address <= 63);

			// ETS 300 706, chapter 12.3.1, table 27: set active position
			if ((mode == 0x04) && (row_address_group == YES)) {
				x26_row = address - 40;
				if (x26_row == 0) x26_row = 24;
				x26_col = 0;
			}

			// ETS 300 706, chapter 12.3.1, table 27: termination marker
			if ((mode >= 0x11) && (mode <= 0x1f) && (row_address_group == YES)) break;

			// ETS 300 706, chapter 12.3.1, table 27: character from G2 set
			if ((mode == 0x0f) && (row_address_group == NO)) {
				x26_col = address;
				if (data > 31) page_buffer.text[x26_row][x26_col] = G2[0][data - 0x20];
			}

			// ETS 300 706, chapter 12.3.1, table 27: G0 character with diacritical mark
			if ((mode >= 0x11) && (mode <= 0x1f) && (row_address_group == NO)) {
				x26_col = address;

				// A - Z
				if ((data >= 65) && (data <= 90)) page_buffer.text[x26_row][x26_col] = G2_ACCENTS[mode - 0x11][data - 65];
				// a - z
				else if ((data >= 97) && (data <= 122)) page_buffer.text[x26_row][x26_col] = G2_ACCENTS[mode - 0x11][data - 71];
				// other
				else page_buffer.text[x26_row][x26_col] = telx_to_ucs2(data);
			}
		}
	}
	else if (y == 28) {
		if (states.x28_not_implemented_notified == NO) {
			dbg_print (DMT_TELETEXT, "- Packet X/28 received; not yet implemented; you won't be able to use secondary language etc.\n");
			states.x28_not_implemented_notified = YES;
		}
	}
	else if (y == 29) {
			if (states.m29_not_implemented_notified == NO) {
				dbg_print (DMT_TELETEXT, "- Packet M/29 received; not yet implemented; you won't be able to use secondary language etc.\n");
				states.m29_not_implemented_notified = YES;
			}
	}
	else if ((y == 30) && (m == 8)) {
		// ETS 300 706, chapter 9.8: Broadcast Service Data Packets
		if (states.programme_info_processed == NO) {
			// ETS 300 706, chapter 9.8.1: Packet 8/30 Format 1
			if (unham_8_4(packet->data[0]) < 2) {
				fprintf(stderr, "- Programme Identification Data = ");
				for (uint8_t i = 20; i < 40; i++) {
					char u[4] = {0, 0, 0, 0};
					ucs2_to_utf8(u, telx_to_ucs2(packet->data[i]));
					fprintf(stderr, "%s", u);
				}
				fprintf(stderr, "\n");

				// OMG! ETS 300 706 stores timestamp in 7 bytes in Modified Julian Day in BCD format + HH:MM:SS in BCD format
				// + timezone as 5-bit count of half-hours from GMT with 1-bit sign
				// In addition all decimals are incremented by 1 before transmission.
				uint32_t t = 0;
				// 1st step: BCD to Modified Julian Day
				t += (packet->data[10] & 0x0f) * 10000;
				t += ((packet->data[11] & 0xf0) >> 4) * 1000;
				t += (packet->data[11] & 0x0f) * 100;
				t += ((packet->data[12] & 0xf0) >> 4) * 10;
				t += (packet->data[12] & 0x0f);
				t -= 11111;
				// 2nd step: conversion Modified Julian Day to unix timestamp
				t = (t - 40587) * 86400;
				// 3rd step: add time
				t += 3600 * ( ((packet->data[13] & 0xf0) >> 4) * 10 + (packet->data[13] & 0x0f) );
				t +=   60 * ( ((packet->data[14] & 0xf0) >> 4) * 10 + (packet->data[14] & 0x0f) );
				t +=        ( ((packet->data[15] & 0xf0) >> 4) * 10 + (packet->data[15] & 0x0f) );
				t -= 40271;
				// 4th step: conversion to time_t
				time_t t0 = (time_t)t;
				// ctime output itself is \n-ended
				fprintf(stderr, "- Universal Time Co-ordinated = %s", ctime(&t0));

				dbg_print (DMT_TELETEXT, "- Transmission mode = %s\n", (transmission_mode == TRANSMISSION_MODE_SERIAL ? "serial" : "parallel"));

				states.programme_info_processed = YES;
			}
		}
	}
}

void tlt_process_pes_packet(uint8_t *buffer, uint16_t size) {
	tlt_packet_counter++;
	if (size < 6) return;

	// Packetized Elementary Stream (PES) 32-bit start code
	uint64_t pes_prefix = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
	uint8_t pes_stream_id = buffer[3];

	// check for PES header
	if (pes_prefix != 0x000001) return;

	// stream_id is not "Private Stream 1" (0xbd)
	if (pes_stream_id != 0xbd) return;

	// PES packet length
	// ETSI EN 301 775 V1.2.1 (2003-05) chapter 4.3: (N × 184) - 6 + 6 B header
	uint16_t pes_packet_length = 6 + ((buffer[4] << 8) | buffer[5]);
	// Can be zero. If the "PES packet length" is set to zero, the PES packet can be of any length.
	// A value of zero for the PES packet length can be used only when the PES packet payload is a video elementary stream.
	if (pes_packet_length == 6) return;

	// truncate incomplete PES packets
	if (pes_packet_length > size) pes_packet_length = size;

	uint8_t optional_pes_header_included = NO;
	uint16_t optional_pes_header_length = 0;
	// optional PES header marker bits (10.. ....)
	if ((buffer[6] & 0xc0) == 0x80) {
		optional_pes_header_included = YES;
		optional_pes_header_length = buffer[8];
	}

	static uint8_t using_pts = UNDEF;
	if (using_pts == UNDEF) {
		if ((optional_pes_header_included == YES) && ((buffer[7] & 0x80) > 0)) {
			using_pts = YES;
			dbg_print (DMT_TELETEXT, "- PID 0xbd PTS available\n");
		} else {
			using_pts = NO;
			dbg_print (DMT_TELETEXT, "- PID 0xbd PTS unavailable, using TS PCR\n");
		}
	}

	uint32_t t = 0;
	// If there is no PTS available, use global PCR
	if (using_pts == NO) t = global_timestamp;
	else {
		// PTS is 33 bits wide, however, timestamp in ms fits into 32 bits nicely (PTS/90)
		// presentation and decoder timestamps use the 90 KHz clock, hence PTS/90 = [ms]
		uint64_t pts = 0;
		// __MUST__ assign value to uint64_t and __THEN__ rotate left by 29 bits
		// << is defined for signed int (as in "C" spec.) and overflow occures
		pts = (buffer[9] & 0x0e);
		pts <<= 29;
		pts |= (buffer[10] << 22);
		pts |= ((buffer[11] & 0xfe) << 14);
		pts |= (buffer[12] << 7);
		pts |= ((buffer[13] & 0xfe) >> 1);
		t = (uint32_t) (pts / 90);
	}

	static int64_t delta = 0;
	static uint32_t t0 = 0;
	if (states.pts_initialized == NO) {
		delta = (uint64_t) (1000 * tlt_config.offset - t);
		t0 = t;
		states.pts_initialized = YES;
	}
	// TODO: How the hell have I calculated this constant?!! It's correct, however I have no idea why. :-D
	if (t < t0) delta += 95443718;
	t0 = t;
	last_timestamp = t + delta;

	// skip optional PES header and process each 46-byte teletext packet
	uint16_t i = 7;
	if (optional_pes_header_included == YES) i += 3 + optional_pes_header_length;
	while (i <= pes_packet_length - 6) {
		uint8_t data_unit_id = buffer[i++];
		uint8_t data_unit_len = buffer[i++];

		if ((data_unit_id == DATA_UNIT_EBU_TELETEXT_NONSUBTITLE) || (data_unit_id == DATA_UNIT_EBU_TELETEXT_SUBTITLE)) {
			// teletext payload has always size 44 bytes
			if (data_unit_len == 44) {
				// reverse endianess (via lookup table), ETS 300 706, chapter 7.1
				for (uint8_t j = 0; j < data_unit_len; j++) buffer[i + j] = REVERSE_8[buffer[i + j]];

				// FIXME: This explicit type conversion could be a problem some day -- do not need to be platform independant
				process_telx_packet((data_unit_t) data_unit_id, (teletext_packet_payload_t *)&buffer[i], last_timestamp);
			}
		}

		i += data_unit_len;
	}
}

void analyze_pat(uint8_t *buffer, uint8_t size) {
	if (size < 7) return;

	pat_t pat = { 0 };
	pat.pointer_field = buffer[0];

//!
if (pat.pointer_field > 0) {
	fprintf(stderr, "! pat.pointer_field > 0 (0x%02x)\n\n", pat.pointer_field);
	exit(EXIT_FAILURE);
}

	pat.table_id = buffer[1];
	if (pat.table_id == 0x00) {
		pat.section_length = ((buffer[2] & 0x03) << 8) | buffer[3];
		pat.current_next_indicator = buffer[6] & 0x01;
		// already valid PAT
		if (pat.current_next_indicator == 1) {
			uint16_t i = 9;
			while ((i < 9 + (pat.section_length - 5 - 4)) && (i < size)) {
				pat_section_t section = { 0 };
				section.program_num = (buffer[i] << 8) | buffer[i + 1];
				section.program_pid = ((buffer[i + 2] & 0x1f) << 8) | buffer[i + 3];

				if (in_array(pmt_map, pmt_map_length, section.program_pid) == NO) {
					if (pmt_map_length < TS_PMT_MAP_SIZE) {
						pmt_map[pmt_map_length++] = section.program_pid;
#ifdef DEBUG
						fprintf(stderr, "- Found PMT for SID %"PRIu16" (0x%x)\n", section.program_num, section.program_num);
#endif
					}
				}

				i += 4;
			}
		}
	}
}

void analyze_pmt(uint8_t *buffer, uint8_t size) {
	if (size < 7) return;

	pmt_t pmt = { 0 };
	pmt.pointer_field = buffer[0];

//!
if (pmt.pointer_field > 0) {
	fprintf(stderr, "! pmt.pointer_field > 0 (0x%02x)\n\n", pmt.pointer_field);
	exit(EXIT_FAILURE);
}

	pmt.table_id = buffer[1];
	if (pmt.table_id == 0x02) {
		pmt.section_length = ((buffer[2] & 0x03) << 8) | buffer[3];
		pmt.program_num = (buffer[4] << 8) | buffer[5];
		pmt.current_next_indicator = buffer[6] & 0x01;
		pmt.pcr_pid = ((buffer[9] & 0x1f) << 8) | buffer[10];
		pmt.program_info_length = ((buffer[11] & 0x03) << 8) | buffer[12];
		// already valid PMT
		if (pmt.current_next_indicator == 1) {
			uint16_t i = 13 + pmt.program_info_length;
			while ((i < 13 + (pmt.program_info_length + pmt.section_length - 4 - 9)) && (i < size)) {
				pmt_program_descriptor_t desc = { 0 };
				desc.stream_type = buffer[i];
				desc.elementary_pid = ((buffer[i + 1] & 0x1f) << 8) | buffer[i + 2];
				desc.es_info_length = ((buffer[i + 3] & 0x03) << 8) | buffer[i + 4];

				uint8_t descriptor_tag = buffer[i + 5];
				// descriptor_tag: 0x45 = VBI_data_descriptor, 0x46 = VBI_teletext_descriptor, 0x56 = teletext_descriptor
				if ((desc.stream_type == 0x06) && ((descriptor_tag == 0x45) || (descriptor_tag == 0x46) || (descriptor_tag == 0x56))) {
					if (in_array(pmt_ttxt_map, pmt_ttxt_map_length, desc.elementary_pid) == NO) {
						if (pmt_ttxt_map_length < TS_PMT_TTXT_MAP_SIZE) {
							pmt_ttxt_map[pmt_ttxt_map_length++] = desc.elementary_pid;
							if (tlt_config.tid == 0) tlt_config.tid = desc.elementary_pid;
							fprintf(stderr, "- Found VBI/teletext stream ID %"PRIu16" (0x%x) for SID %"PRIu16" (0x%x)\n", desc.elementary_pid, desc.elementary_pid, pmt.program_num, pmt.program_num);
						}
					}
				}

				i += 5 + desc.es_info_length;
			}
		}
	}
}

// graceful exit support
uint8_t exit_request = NO;

void signal_handler(int sig) {
	if ((sig == SIGINT) || (sig == SIGTERM)) {
		fprintf(stderr, "- SIGINT/SIGTERM received, performing graceful exit\n");
		exit_request = YES;
	}
}

// Called only when teletext is detected or forced and it's going to be used for extraction. 
void telxcc_init(void)
{
	if (!telxcc_inited)
	{
		telxcc_inited=1;
		fdprintf(wbout1.fh, "\xef\xbb\xbf");		
	}
}

// Close output
void telxcc_close(void)
{
	if (telxcc_inited)
	{
		// output any pending close caption
		if (page_buffer.tainted == YES) {
			// this time we do not subtract any frames, there will be no more frames
			page_buffer.hide_timestamp = last_timestamp;
			process_page(&page_buffer);
		}	

		if ((tlt_frames_produced == 0) && (tlt_config.nonempty == YES)) {
			fdprintf(wbout1.fh, "1\r\n00:00:00,000 --> 00:00:10,000\r\n(no closed captions available)\r\n\r\n");			
			tlt_frames_produced++;
		}
	}
}

int main_telxcc (int argc, char *argv[]) {
/*
	// command line params parsing
	for (uint8_t i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0) {
			fprintf(stderr, "Usage: telxcc [-h] [-v] [-p PAGE] [-t TID] [-o OFFSET] [-n] [-1] [-c]\n");
			fprintf(stderr, "  STDIN       transport stream\n");
			fprintf(stderr, "  STDOUT      subtitles in SubRip SRT file format (UTF-8 encoded)\n");
			fprintf(stderr, "  -h          this help text\n");
			fprintf(stderr, "  -v          be verbose\n");
			fprintf(stderr, "  -p PAGE     teletext page number carrying closed captions\n");
			fprintf(stderr, "  -t TID      transport stream PID of teletext data sub-stream\n");
			fprintf(stderr, "  -o OFFSET   subtitles offset in seconds\n");
			fprintf(stderr, "  -n          do not print UTF-8 BOM characters to the file\n");
			fprintf(stderr, "  -1          produce at least one (dummy) frame\n");
			fprintf(stderr, "  -c          output colour information in font HTML tags\n");
			fprintf(stderr, "\n");
			exit(EXIT_SUCCESS);
		}
		else if ((strcmp(argv[i], "-t") == 0) && (argc > i + 1))
			tlt_config.tid = atoi(argv[++i]);
		else if ((strcmp(argv[i], "-o") == 0) && (argc > i + 1))
			tlt_config.offset = atof(argv[++i]);
		else if (strcmp(argv[i], "-n") == 0)
			tlt_config.bom = NO;
		else if (strcmp(argv[i], "-1") == 0)
			tlt_config.nonempty = YES;
		else if (strcmp(argv[i], "-c") == 0)
			tlt_config.colours = YES;
		else {
			fprintf(stderr, "- Unknown option %s\n", argv[i]);
			exit(EXIT_FAILURE);
		}
	}
*/

	FILE *infile=fopen ("f:\\TELETEXTO.ts","rb");

	// full buffering -- disables flushing after CR/FL, we will flush manually whole SRT frames
	// setvbuf(stdout, (char*)NULL, _IOFBF, 0);

	// print UTF-8 BOM chars
	if (tlt_config.bom == YES) {
		fdprintf(wbout1.fh, "\xef\xbb\xbf");		
	}
 
	// TS packet buffer
	uint8_t ts_buffer[TS_PACKET_SIZE] = { 0 };

	// 255 means not set yet
	uint8_t continuity_counter = 255;

	// PES packet buffer
	uint8_t payload_buffer[PAYLOAD_BUFFER_SIZE] = { 0 };
	uint16_t payload_counter = 0;

	// reading input
	while ((exit_request == NO) && (fread(&ts_buffer, 1, TS_PACKET_SIZE, infile) == TS_PACKET_SIZE)) {
		// Transport Stream Header
		// We do not use buffer to struct loading (e.g. ts_packet_t *header = (ts_packet_t *)buffer;)
		// -- struct packing is platform dependant and not performing well.
		ts_packet_t header = { 0 };
		header.sync = ts_buffer[0];
		header.transport_error = (ts_buffer[1] & 0x80) >> 7;
		header.payload_unit_start = (ts_buffer[1] & 0x40) >> 6;
		header.transport_priority = (ts_buffer[1] & 0x20) >> 5;
		header.pid = ((ts_buffer[1] & 0x1f) << 8) | ts_buffer[2];
		header.scrambling_control = (ts_buffer[3] & 0xc0) >> 6;
		header.adaptation_field_exists = (ts_buffer[3] & 0x20) >> 5;
		header.continuity_counter = ts_buffer[3] & 0x0f;
		//uint8_t ts_payload_exists = (ts_buffer[3] & 0x10) >> 4;

		uint8_t af_discontinuity = 0;
		if (header.adaptation_field_exists > 0) {
			af_discontinuity = (ts_buffer[5] & 0x80) >> 7;
		}

		// not TS packet?
		if (header.sync != 0x47) 
			fatal (EXIT_UNSUPPORTED, "- Invalid TS packet header\n- telxcc does not work with unaligned TS.\n\n");

		// uncorrectable error?
		if (header.transport_error > 0) {
			dbg_print (DMT_TELETEXT, "- Uncorrectable TS packet error (received CC %1x)\n", header.continuity_counter);
			continue;
		}

		// if available, calculate current PCR
		if (header.adaptation_field_exists > 0) {
			// PCR in adaptation field
			uint8_t af_pcr_exists = (ts_buffer[5] & 0x10) >> 4;
			if (af_pcr_exists > 0) {
				uint64_t pts = 0;
				pts |= (ts_buffer[6] << 25);
				pts |= (ts_buffer[7] << 17);
				pts |= (ts_buffer[8] << 9);
				pts |= (ts_buffer[9] << 1);
				pts |= (ts_buffer[10] >> 7);
				global_timestamp = (uint32_t) pts / 90;
				pts = ((ts_buffer[10] & 0x01) << 8);
				pts |= ts_buffer[11];
				global_timestamp += (uint32_t) (pts / 27000);
			}
		}

		// null packet
		if (header.pid == 0x1fff) continue;

		// TID not specified, autodetect via PAT/PMT
		if (tlt_config.tid == 0) {
			// process PAT
			if (header.pid == 0x0000) {
				analyze_pat(&ts_buffer[4], TS_PACKET_PAYLOAD_SIZE);
				continue;
			}

			// process PMT
			if (in_array(pmt_map, pmt_map_length, header.pid) == YES) {
				analyze_pmt(&ts_buffer[4], TS_PACKET_PAYLOAD_SIZE);
				continue;
			}
		}

		if (tlt_config.tid == header.pid) {
			// TS continuity check
			if (continuity_counter == 255) continuity_counter = header.continuity_counter;
			else {
				if (af_discontinuity == 0) {
					continuity_counter = (continuity_counter + 1) % 16;
					if (header.continuity_counter != continuity_counter) {
						dbg_print (DMT_TELETEXT, "- Missing TS packet, flushing pes_buffer (expected CC %1x, received CC %1x, TS discontinuity %s, TS priority %s)\n",
							continuity_counter, header.continuity_counter, (af_discontinuity ? "YES" : "NO"), (header.transport_priority ? "YES" : "NO"));
						payload_counter = 0;
						continuity_counter = 255;
					}
				}
			}

			// waiting for first payload_unit_start indicator
			if ((header.payload_unit_start == 0) && (payload_counter == 0)) continue;

			// proceed with payload buffer
			if ((header.payload_unit_start > 0) && (payload_counter > 0)) tlt_process_pes_packet(payload_buffer, payload_counter);

			// new payload frame start
			if (header.payload_unit_start > 0) payload_counter = 0;

			// add payload data to buffer
			if (payload_counter < (PAYLOAD_BUFFER_SIZE - TS_PACKET_PAYLOAD_SIZE)) {
				memcpy(&payload_buffer[payload_counter], &ts_buffer[4], TS_PACKET_PAYLOAD_SIZE);
				payload_counter += TS_PACKET_PAYLOAD_SIZE;
				tlt_packet_counter++;
			}
			else 
				dbg_print (DMT_TELETEXT, "- packet payload size exceeds payload_buffer size, probably not teletext stream\n");
		}
	}


	VERBOSE_ONLY {
		if (tlt_config.tid == 0) dbg_print (DMT_TELETEXT, "- No teletext PID specified, no suitable PID found in PAT/PMT tables. Please specify teletext PID via -t parameter.\n");
		if (tlt_frames_produced == 0) dbg_print (DMT_TELETEXT, "- No frames produced. CC teletext page number was probably wrong.\n");
		dbg_print (DMT_TELETEXT, "- There were some CC data carried via pages = ");
		// We ignore i = 0xff, because 0xffs are teletext ending frames
		for (uint16_t i = 0; i < 255; i++)
			for (uint8_t j = 0; j < 8; j++) {
				uint8_t v = cc_map[i] & (1 << j);
				if (v > 0) fprintf(stderr, "%03x ", ((j + 1) << 8) | i);
			}
		fprintf(stderr, "\n");
	}


	return EXIT_SUCCESS;
}
 
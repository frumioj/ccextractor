#include "ccextractor.h"

// Program Identification Number (Start Time) for current program
int current_xds_min=-1;
int current_xds_hour=-1;
int current_xds_date=-1;
int current_xds_month=-1;
int current_program_type_reported=0; // No.
int xds_start_time_shown=0;
int xds_program_length_shown=0;
int xds_program_type_shown=0;
char xds_program_description[8][33];

char current_xds_network_name[33]; 
char current_xds_program_name[33]; 
char current_xds_call_letters[7];
char current_xds_program_type[33];

const char *XDSclasses[]=
{
	"Current",
	"Future",
	"Channel",
	"Miscellaneous",
	"Public service",
	"Reserved",
	"Private data",
	"End"
};

const char *XDSProgramTypes[]=
{
	"Education","Entertainment", "Movie", "News", "Religious",
	"Sports", "Other", "Action","Advertisement", "Animated",
	"Anthology","Automobile","Awards","Baseball","Basketball",
	"Bulletin","Business","Classical","College","Combat",
	"Comedy","Commentary","Concert","Consumer","Contemporary",
	"Crime","Dance","Documentary","Drama","Elementary",
	"Erotica","Exercise","Fantasy","Farm","Fashion",
	"Fiction","Food","Football","Foreign","Fund-Raiser",
	"Game/Quiz","Garden","Golf","Government","Health",
	"High_School","History","Hobby","Hockey","Home",
	"Horror","Information","Instruction","International","Interview",
	"Language","Legal","Live","Local","Math",
	"Medical","Meeting","Military","Mini-Series","Music",
	"Mystery","National","Nature","Police","Politics",
	"Premiere","Pre-Recorded","Product","Professional","Public",
	"Racing","Reading","Repair","Repeat","Review",
	"Romance","Science","Series","Service","Shopping",
	"Soap_Opera","Special","Suspense","Talk","Technical",
	"Tennis","Travel","Variety","Video","Weather",
	"Western"
};

#define XDS_CLASS_CURRENT	0
#define XDS_CLASS_FUTURE	1
#define XDS_CLASS_CHANNEL	2
#define XDS_CLASS_MISC		3
#define XDS_CLASS_PUBLIC	4
#define XDS_CLASS_RESERVED	5
#define XDS_CLASS_PRIVATE	6
#define XDS_CLASS_END		7

// Types for the classes current and future
#define XDS_TYPE_PIN_START_TIME	1
#define XDS_TYPE_LENGH_AND_CURRENT_TIME	2
#define XDS_TYPE_PROGRAM_NAME 3
#define XDS_TYPE_PROGRAM_TYPE 4
#define XDS_TYPE_CONTENT_ADVISORY 5
#define XDS_TYPE_CGMS 8 // Copy Generation Management System
#define XDS_TYPE_PROGRAM_DESC_1 0x10
#define XDS_TYPE_PROGRAM_DESC_2 0x11
#define XDS_TYPE_PROGRAM_DESC_3 0x12
#define XDS_TYPE_PROGRAM_DESC_4 0x13
#define XDS_TYPE_PROGRAM_DESC_5 0x14
#define XDS_TYPE_PROGRAM_DESC_6 0x15
#define XDS_TYPE_PROGRAM_DESC_7 0x16
#define XDS_TYPE_PROGRAM_DESC_8 0x17

// Types for the class channel
#define XDS_TYPE_NETWORK_NAME 1	
#define XDS_TYPE_CALL_LETTERS_AND_CHANNEL 2	
#define XDS_TYPE_TSID 4	// Transmission Signal Identifier

// Types for miscellaneous packets
#define XDS_TYPE_TIME_OF_DAY 1	

#define NUM_XDS_BUFFERS 9  // CEA recommends no more than one level of interleaving. Play it safe
#define NUM_BYTES_PER_PACKET 35 // Class + type (repeated for convenience) + data + zero

struct xds_buffer
{
	unsigned in_use;
	int xds_class;
	int xds_type;
	unsigned char bytes[NUM_BYTES_PER_PACKET]; // Class + type (repeated for convenience) + data + zero
	unsigned char used_bytes;
} xds_buffers[NUM_XDS_BUFFERS]; 

int cur_xds_buffer_idx=-1;
int cur_xds_packet_class=-1;
unsigned char *cur_xds_payload;
int cur_xds_payload_length;
int cur_xds_packet_type;


void xds_init()
{
	for (int i=0;i<NUM_XDS_BUFFERS;i++)
	{
		xds_buffers[i].in_use=0;
		xds_buffers[i].xds_class=-1;
		xds_buffers[i].xds_type=-1;
		xds_buffers[i].used_bytes=0;
		memset (xds_buffers[i].bytes , 0, NUM_BYTES_PER_PACKET);
	}
	for (int i=0; i<9; i++)	
		memset (xds_program_description,0,32); 
	
	memset (current_xds_network_name,0,33); 
	memset (current_xds_program_name,0,33); 
	memset (current_xds_call_letters,0,7);
	memset (current_xds_program_type,0,33); 
}

void xds_debug_test()
{
	process_xds_bytes (0x05,0x02);
	process_xds_bytes (0x20,0x20);
	do_end_of_xds (0x2a);

}

void xds_cea608_test()
{
	/* This test is the sample data that comes in CEA-608. It sets the program name
	to be "Star Trek". The checksum is 0x1d and the validation must succeed. */
	process_xds_bytes (0x01,0x03);
	process_xds_bytes (0x53,0x74);
	process_xds_bytes (0x61,0x72);
	process_xds_bytes (0x20,0x54);
	process_xds_bytes (0x72,0x65);
	process_xds_bytes (0x02,0x03);
	process_xds_bytes (0x02,0x03);
	process_xds_bytes (0x6b,0x00);
	do_end_of_xds (0x1d);
}

int how_many_used()
{
	int c=0;
	for (int i=0;i<NUM_XDS_BUFFERS;i++)
		if (xds_buffers[i].in_use)
			c++;
	return c;

}

void clear_xds_buffer (int num)
{
	xds_buffers[num].in_use=0;
	xds_buffers[num].xds_class=-1;
	xds_buffers[num].xds_type=-1;
	xds_buffers[num].used_bytes=0;
	memset (xds_buffers[num].bytes , 0, NUM_BYTES_PER_PACKET);
}

void process_xds_bytes (const unsigned char hi, int lo)
{	
	int is_new;
	if (hi>=0x01 && hi<=0x0f)
	{
		int xds_class=(hi-1)/2; // Start codes 1 and 2 are "class type" 0, 3-4 are 2, and so on.
		is_new=hi%2; // Start codes are even
		dbg_print(DMT_XDS, "XDS Start: %u.%u  Is new: %d  | Class: %d (%s), Used buffers: %d\n",hi,lo, is_new,xds_class, XDSclasses[xds_class], how_many_used());
		int first_free_buf=-1;
		int matching_buf=-1;
		for (int i=0;i<NUM_XDS_BUFFERS;i++)
		{
			if (xds_buffers[i].in_use && 
				xds_buffers[i].xds_class==xds_class &&
				xds_buffers[i].xds_type==lo)
			{
				matching_buf=i;
				break;
			}
			if (first_free_buf==-1 && !xds_buffers[i].in_use)
				first_free_buf=i;
		}
		/* Here, 3 possibilities: 
			1) We already had a buffer for this class/type and matching_buf points to it
			2) We didn't have a buffer for this class/type and first_free_buf points to an unused one
			3) All buffers are full and we will have to skip this packet.
			*/
		if (matching_buf==-1 && first_free_buf==-1)
		{
			mprint ("Note: All XDS buffers full (bug or suicidal stream). Ignoring this one (%d,%d).\n",xds_class,lo);
			cur_xds_buffer_idx=-1;
			return;

		}
		cur_xds_buffer_idx=(matching_buf!=-1)? matching_buf:first_free_buf;

		if (is_new || !xds_buffers[cur_xds_buffer_idx].in_use)
		{
			// Whatever we had before we discard; must belong to an interrupted packet
			xds_buffers[cur_xds_buffer_idx].xds_class=xds_class;
			xds_buffers[cur_xds_buffer_idx].xds_type=lo;
			xds_buffers[cur_xds_buffer_idx].used_bytes=0;
			xds_buffers[cur_xds_buffer_idx].in_use=1;
			memset (xds_buffers[cur_xds_buffer_idx].bytes,0,NUM_BYTES_PER_PACKET);						
		}
		if (!is_new)
		{
			// Continue codes aren't added to packet.
			return;
		}
	}
	else
	{
		// Informational: 00, or 0x20-0x7F, so 01-0x1f forbidden
		dbg_print(DMT_XDS, "XDS: %02X.%02X (%c, %c)\n",hi,lo,hi,lo);
		if ((hi>0 && hi<=0x1f) || (lo>0 && lo<=0x1f))
		{
			mprint ("\rNote: Illegal XDS data");
			return;
		}
	}
	if (xds_buffers[cur_xds_buffer_idx].used_bytes<=32)
	{
		// Should always happen
		xds_buffers[cur_xds_buffer_idx].bytes[xds_buffers[cur_xds_buffer_idx].used_bytes++]=hi;
		xds_buffers[cur_xds_buffer_idx].bytes[xds_buffers[cur_xds_buffer_idx].used_bytes++]=lo;
		xds_buffers[cur_xds_buffer_idx].bytes[xds_buffers[cur_xds_buffer_idx].used_bytes]=0; 
	}
}

int xds_do_current_and_future ()
{
	int was_proc=0;
	switch (cur_xds_packet_type)
	{
		case XDS_TYPE_PIN_START_TIME:
			{
				was_proc=1;
				if (cur_xds_payload_length<7) // We need 4 data bytes
					break;
				int min=cur_xds_payload[2] & 0x3f; // 6 bits
				int hour = cur_xds_payload[3] & 0x1f; // 5 bits
				int date = cur_xds_payload[4] & 0x1f; // 5 bits
				int month = cur_xds_payload[5] & 0xf; // 4 bits
				int changed=0;
				if (current_xds_min!=min ||
					current_xds_hour!=hour ||
					current_xds_date!=date ||
					current_xds_month!=month)
				{
					changed=1;
					xds_start_time_shown=0;
					current_xds_min=min;
					current_xds_hour=hour;
					current_xds_date=date;
					current_xds_month=month;
				}

				dbg_print(DMT_XDS, "PIN (Start Time): %s  %02d-%02d %02d:%02d\n",
						(cur_xds_packet_class==XDS_CLASS_CURRENT?"Current":"Future"),
						date,month,hour,min);

				if (!xds_start_time_shown && cur_xds_packet_class==XDS_CLASS_CURRENT)
				{
					mprint ("\rXDS: Program changed.\n");
					mprint ("XDS program start time (DD/MM HH:MM) %02d-%02d %02d:%02d\n",date,month,hour,min);
					activity_xds_program_identification_number (current_xds_min, 
						current_xds_hour, current_xds_date, current_xds_month);
					xds_start_time_shown=1;
				}			
			}
			break;
		case XDS_TYPE_LENGH_AND_CURRENT_TIME:
			{
				was_proc=1;
				if (cur_xds_payload_length<5) // We need 2 data bytes
					break;
				int min=cur_xds_payload[2] & 0x3f; // 6 bits
				int hour = cur_xds_payload[3] & 0x1f; // 5 bits
				if (!xds_program_length_shown)				
					mprint ("\rXDS: Program length (HH:MM): %02d:%02d  ",hour,min);
				else
					dbg_print(DMT_XDS, "\rXDS: Program length (HH:MM): %02d:%02d  ",hour,min);
				if (cur_xds_payload_length>6) // Next two bytes (optional) available
				{
					int el_min=cur_xds_payload[4] & 0x3f; // 6 bits
					int el_hour = cur_xds_payload[5] & 0x1f; // 5 bits
					if (!xds_program_length_shown)
						mprint ("Elapsed (HH:MM): %02d:%02d",el_hour,el_min);
					else
						dbg_print(DMT_XDS, "Elapsed (HH:MM): %02d:%02d",el_hour,el_min);
				}
				if (cur_xds_payload_length>8) // Next two bytes (optional) available
				{
					int el_sec=cur_xds_payload[6] & 0x3f; // 6 bits							
					if (!xds_program_length_shown)
						dbg_print(DMT_XDS, ":%02d",el_sec);
				}
				if (!xds_program_length_shown)
					printf ("\n");
				else
					dbg_print(DMT_XDS, "\n");
				xds_program_length_shown=1;
			}
			break;
		case XDS_TYPE_PROGRAM_NAME:
			{
				was_proc=1;
				char xds_program_name[33];
				int i;
				for (i=2;i<cur_xds_payload_length-1;i++)
					xds_program_name[i-2]=cur_xds_payload[i];
				xds_program_name[i-2]=0;
				dbg_print(DMT_XDS, "\rXDS Program name: %s\n",xds_program_name);
				if (cur_xds_packet_class==XDS_CLASS_CURRENT && 
					strcmp (xds_program_name, current_xds_program_name)) // Change of program
				{
					if (!gui_mode_reports)
						mprint ("\rXDS Notice: Program is now %s\n", xds_program_name);
					strcpy (current_xds_program_name,xds_program_name);
					activity_xds_program_name (xds_program_name);
				}
				break;
			}
			break;
		case XDS_TYPE_PROGRAM_TYPE:
			was_proc=1;					
			if (cur_xds_payload_length<5) // We need 2 data bytes
				break;
			if (current_program_type_reported)
			{
				// Check if we should do it again
				for (int i=0;i<cur_xds_payload_length ; i++)
				{
					if (cur_xds_payload[i]!=current_xds_program_type[i])
					{
						current_program_type_reported=0;
						break;
					}
				}
			}
			if (!(debug_mask & DMT_XDS) && current_program_type_reported)
				break;
			memcpy (current_xds_program_type,cur_xds_payload,cur_xds_payload_length);
			current_xds_program_type[cur_xds_payload_length]=0;
			mprint ("\rXDS Program Type: ");
			for (int i=2;i<cur_xds_payload_length - 1; i++)
			{								
				if (cur_xds_payload[i]==0) // Padding
					continue;														
				mprint ("[%02X-", cur_xds_payload[i]);
				if (cur_xds_payload[i]>=0x20 && cur_xds_payload[i]<0x7F)
					mprint ("%s",XDSProgramTypes[cur_xds_payload[i]-0x20]); 
				else
					mprint ("ILLEGAL VALUE");
				mprint ("] ");						
			} 
			mprint ("\n");
			current_program_type_reported=1;
			break; 
		case XDS_TYPE_CONTENT_ADVISORY: 
			was_proc=1; // For now at least - not important.
			break;
		case XDS_TYPE_CGMS:
			was_proc=1; // For now at least - not important.
			break;
		case XDS_TYPE_PROGRAM_DESC_1:
		case XDS_TYPE_PROGRAM_DESC_2:
		case XDS_TYPE_PROGRAM_DESC_3:
		case XDS_TYPE_PROGRAM_DESC_4:
		case XDS_TYPE_PROGRAM_DESC_5:
		case XDS_TYPE_PROGRAM_DESC_6:
		case XDS_TYPE_PROGRAM_DESC_7:
		case XDS_TYPE_PROGRAM_DESC_8:
			{
				was_proc=1;
				int changed=0;
				char xds_desc[33];
				int i;
				for (i=2;i<cur_xds_payload_length-1;i++)
					xds_desc[i-2]=cur_xds_payload[i];
				xds_desc[i-2]=0;
				
				if (xds_desc[0])
				{					
					int line_num=cur_xds_packet_type-XDS_TYPE_PROGRAM_DESC_1;
					if (strcmp (xds_desc, xds_program_description[line_num]))
						changed=1;
					if (changed)
					{
						mprint ("\rXDS description line %d: %s\n",line_num,xds_desc);
						strcpy (xds_program_description[line_num], xds_desc);
					}
					else
						dbg_print(DMT_XDS, "\rXDS description line %d: %s\n",line_num,xds_desc);
					activity_xds_program_description (line_num, xds_desc);
				}
				break;
			}
	}
	return was_proc;
}

int xds_do_channel ()
{
	int was_proc=0;
	switch (cur_xds_packet_type)
	{
		case XDS_TYPE_NETWORK_NAME:
			was_proc=1;
			char xds_network_name[33];
			int i;
			for (i=2;i<cur_xds_payload_length-1;i++)
				xds_network_name[i-2]=cur_xds_payload[i];
			xds_network_name[i-2]=0;
			dbg_print(DMT_XDS, "XDS Network name: %s\n",xds_network_name);
			if (strcmp (xds_network_name, current_xds_network_name)) // Change of station
			{
				mprint ("XDS Notice: Network is now %s\n", xds_network_name);
				strcpy (current_xds_network_name,xds_network_name);
			}
			break;
		case XDS_TYPE_CALL_LETTERS_AND_CHANNEL:
			{
				was_proc=1;
				char xds_call_letters[33];
				if (cur_xds_payload_length<7) // We need 4-6 data bytes
					break;												
				for (i=2;i<cur_xds_payload_length-1;i++)
				{
					if (cur_xds_payload)
						xds_call_letters[i-2]=cur_xds_payload[i];
				}
				xds_call_letters[i-2]=0;
				
				dbg_print(DMT_XDS, "XDS Network call letters: %s\n",xds_call_letters);
				if (strcmp (xds_call_letters, current_xds_call_letters)) // Change of station
				{
					mprint ("XDS Notice: Network call letters now %s\n", xds_call_letters);
					strcpy (current_xds_call_letters,xds_call_letters);
					activity_xds_network_call_letters (current_xds_call_letters);
				}
			}
			break;
		case XDS_TYPE_TSID:
			// According to CEA-608, data here (4 bytes) are used to identify the 
			// "originating analog licensee". No interesting data for us.
			was_proc=1;
			break;
	}
	return was_proc;
}

int xds_do_misc ()
{
	int was_proc=0;
	switch (cur_xds_packet_type)
	{				
		case XDS_TYPE_TIME_OF_DAY:
			{
			was_proc=1;
			if (cur_xds_payload_length<9) // We need 6 data bytes
				break;
			int min=cur_xds_payload[2] & 0x3f; // 6 bits
			int hour = cur_xds_payload[3] & 0x1f; // 5 bits
			int date = cur_xds_payload[4] & 0x1f; // 5 bits
			int month = cur_xds_payload[5] & 0xf; // 4 bits
			int reset_seconds = (cur_xds_payload[5] & 0x20); 
			int day_of_week = cur_xds_payload[6] & 0x7;
			int year = (cur_xds_payload[7] & 0x3f) + 1990;
			dbg_print(DMT_XDS, "Time of day: (YYYY/MM/DD) %04d/%02d/%02d (HH:SS) %02d:%02d DoW: %d  Reset seconds: %d\n",							
					year,month,date,hour,min, day_of_week, reset_seconds);				
			break;
			}
	} 
	return was_proc;
}

void do_end_of_xds (unsigned char expected_checksum)
{
	if (cur_xds_buffer_idx== -1 || /* Unknown buffer, or not in use (bug) */
		!xds_buffers[cur_xds_buffer_idx].in_use)
		return;
	cur_xds_packet_class=xds_buffers[cur_xds_buffer_idx].xds_class;	
	cur_xds_payload=xds_buffers[cur_xds_buffer_idx].bytes;
	cur_xds_payload_length=xds_buffers[cur_xds_buffer_idx].used_bytes;
	cur_xds_packet_type=cur_xds_payload[1];
	cur_xds_payload[cur_xds_payload_length++]=0x0F; // The end byte itself, added to the packet
	
	int cs=0;
	for (int i=0; i<cur_xds_payload_length;i++)
	{
		cs=cs+cur_xds_payload[i];
		cs=cs & 0x7f; // Keep 7 bits only
		int c=cur_xds_payload[i]&0x7F;
		dbg_print(DMT_XDS, "%02X - %c cs: %02X\n",
			c,(c>=0x20)?c:'?', cs);
	}
	cs=(128-cs) & 0x7F; // Convert to 2's complement & discard high-order bit

	dbg_print(DMT_XDS, "End of XDS. Class=%d (%s), size=%d  Checksum OK: %d   Used buffers: %d\n",
			cur_xds_packet_class,XDSclasses[cur_xds_packet_class],
			cur_xds_payload_length,
			cs==expected_checksum, how_many_used());	

	if (cs!=expected_checksum || cur_xds_payload_length<3)
	{
		dbg_print(DMT_XDS, "Expected checksum: %02X  Calculated: %02X\n", expected_checksum, cs);
		clear_xds_buffer (cur_xds_buffer_idx); 
		return; // Bad packets ignored as per specs
	}
	
	int was_proc=0; /* Indicated if the packet was processed. Not processed means "code to do it doesn't exist yet", not an error. */
	
	switch (cur_xds_packet_class)
	{
		case XDS_CLASS_FUTURE: // Info on future program
			if (!(debug_mask & DMT_XDS)) // Don't bother processing something we don't need
			{
				was_proc=1;
				break; 
			}
		case XDS_CLASS_CURRENT: // Info on current program	
			was_proc = xds_do_current_and_future();
			break;
		case XDS_CLASS_CHANNEL:
			was_proc = xds_do_channel();
			break;
			
		case XDS_CLASS_MISC:
			was_proc = xds_do_misc();
			break;
	}
	
	if (!was_proc)
	{
		mprint ("Note: We found an currently unsupported XDS packet.\n");
	}
	clear_xds_buffer (cur_xds_buffer_idx);

}

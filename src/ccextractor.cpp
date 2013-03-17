/* CCExtractor, cfsmp3 at gmail
Credits: See CHANGES.TXT
License: GPL 2.0
*/
#include <stdio.h>
#include "ccextractor.h"
#include <sys/types.h>
#include <sys/stat.h>

void xds_cea608_test();

extern unsigned char *filebuffer;
extern int bytesinbuffer; // Number of bytes we actually have on buffer

// PTS timing related stuff
LLONG min_pts, max_pts, sync_pts;
LLONG fts_now; // Time stamp of current file (w/ fts_offset, w/o fts_global)
LLONG fts_offset; // Time before first sync_pts
LLONG fts_fc_offset; // Time before first GOP
LLONG fts_max; // Remember the maximum fts that we saw in current file
LLONG fts_global=0; // Duration of previous files (-ve mode), see c1global
// Count 608 (per field) and 708 blocks since last set_fts() call
int cb_field1, cb_field2, cb_708;
int saw_caption_block;

int pts_set; //0 = No, 1 = received, 2 = min_pts set

unsigned pts_big_change;

int MPEG_CLOCK_FREQ = 90000; // This "constant" is part of the standard

// Stuff common to both loops
unsigned char *buffer = NULL;
LLONG past; /* Position in file, if in sync same as ftell()  */
unsigned char *pesheaderbuf = NULL;
LLONG inputsize;
LLONG total_inputsize=0, total_past=0; // Only in binary concat mode

int last_reported_progress;
int processed_enough; // If 1, we have enough lines, time, etc. 

int live_stream=0; /* -1 -> Not a complete file but a live stream, without timeout
                       0 -> A regular file 
                      >0 -> Live stream with a timeout of this value in seconds */
 
// Small buffer to help us with the initial sync
unsigned char startbytes[STARTBYTESLENGTH]; 
unsigned int startbytes_pos;
int startbytes_avail;

/* Stats */
int stat_numuserheaders;
int stat_dvdccheaders;
int stat_scte20ccheaders;
int stat_replay5000headers;
int stat_replay4000headers;
int stat_dishheaders;
int stat_hdtv;
int stat_divicom;
unsigned total_frames_count;
unsigned total_pulldownfields;
unsigned total_pulldownframes;
int cc_stats[4];
int false_pict_header;
int resets_708=0;

/* GOP-based timing */
struct gop_time_code gop_time, first_gop_time, printed_gop;
int saw_gop_header=0;
int frames_since_last_gop=0;
LLONG fts_at_gop_start=0;

/* Time info for timed-transcript */
LLONG ts_start_of_current_line=-1; /* Time at which the first character for current line was received, =-1 no character received yet */
int timestamps_on_transcript=0; /* Write time info on transcripts? */

int max_gop_length=0; // (Maximum) length of a group of pictures
int last_gop_length=0; // Length of the previous group of pictures
int frames_since_ref_time=0;

int gop_rollover=0;

/* Detect gaps in caption stream - only used for dvr-ms/NTSC. */
int CaptionGap=0;

/* Parameters */
#ifdef _WIN32
int buffer_input = 1; // In Windows buffering seems to help
#else
int buffer_input = 0; // In linux, not so much.
#endif
stream_mode_enum stream_mode = SM_ELEMENTARY_OR_NOT_FOUND; // Data parse mode: 0=elementary, 1=transport, 2=program stream, 3=ASF container
stream_mode_enum auto_stream = SM_AUTODETECT;
int use_gop_as_pts = 0; // Use GOP instead of PTS timing
int fix_padding = 0; // Replace 0000 with 8080 in HDTV (needed for some cards)
int rawmode = 0; // Broadcast or DVD
// See -d from http://www.geocities.com/mcpoodle43/SCC_TOOLS/DOCS/SCC_TOOLS.HTML#CCExtract
int extract = 1; // Extract 1st, 2nd or both fields
int cc_channel = 1; // Channel we want to dump in srt mode
LLONG debug_mask=DMT_GENERIC_NOTICES; // dbg_print will use this mask to print or ignore different types
LLONG debug_mask_on_debug=0; // If we're using temp_debug to enable/disable debug "live", this is the mask when temp_debug=1
int messages_target=1; // 0 = nowhere (quiet), 1=stdout, 2=stderr
int cc_to_stdout=0; // If 1, captions go to stdout instead of file
int nosync=0; // Disable syncing
int fullbin=0; // Disable pruning of padding cc blocks
LLONG subs_delay=0; // ms to delay (or advance) subs
int trim_subs=0; // "    Remove spaces at sides?    "
struct boundary_time extraction_start, extraction_end; // Segment we actually process
struct boundary_time startcreditsnotbefore, startcreditsnotafter; // Where to insert start credits, if possible
struct boundary_time startcreditsforatleast, startcreditsforatmost; // How long to display them?
struct boundary_time endcreditsforatleast, endcreditsforatmost;
int startcredits_displayed=0, end_credits_displayed=0;
LLONG last_displayed_subs_ms=0; // When did the last subs end?
LLONG screens_to_process=-1; // How many screenfuls we want?
char *basefilename=NULL; // Input filename without the extension
char **inputfile=NULL; // List of files to process
int input_is_stdin=0; // If 1, then forget about files, read from stdin
const char *extension; // Output extension
int current_file=-1; // If current_file!=1, we are processing *inputfile[current_file]
int direct_rollup=0; // Write roll-up captions directly instead of line by line?
int num_input_files=0; // How many?
int do_cea708=0; // Process 708 data?
int cea708services[63]; // [] -> 1 for services to be processed

int nofontcolor=0; // 1 = don't put <font color> tags 
int notypesetting=0; // 1 = Don't put <i>, <u>, etc typesetting tags
output_format write_format=OF_SRT; // 0=Raw, 1=srt, 2=SMI
encoding_type encoding = ENC_LATIN_1;
int usepicorder = 0; // Force the use of pic_order_cnt_lsb in AVC/H.264 data streams
int auto_myth = 2; // Use myth-tv mpeg code? 0=no, 1=yes, 2=auto
int wtvconvertfix = 0; // Fix broken Windows 7 conversion
int sentence_cap =0 ; // FIX CASE? = Fix case?
char *sentence_cap_file=NULL; // Extra words file?
int binary_concat=1; // Disabled by -ve or --videoedited
int norollup=0; // If 1, write one line at a time
int gui_mode_reports=0; // If 1, output in stderr progress updates so the GUI can grab them
int no_progress_bar=0; // If 1, suppress the output of the progress to stdout
char *output_filename=NULL;
char *out_elementarystream_filename=NULL;
unsigned ts_forced_program=0; // Specific program to process in TS files, if ts_forced_program_selected==1
unsigned ts_forced_program_selected=0; 

// Case arrays
char **spell_lower=NULL;
char **spell_correct=NULL;
int spell_words=0;
int spell_capacity=0;

/* Credit stuff */
char *start_credits_text=NULL;
char *end_credits_text=NULL;

/* Hauppauge support */
unsigned hauppauge_warning_shown=0; // Did we detect a possible Hauppauge capture and told the user already?
unsigned hauppauge_mode=0; // If 1, use PID=1003, process specially and so on
unsigned teletext_warning_shown=0; // Did we detect a possible PAL (with teletext subs) and told the user already?
unsigned telext_mode=TXT_AUTO_NOT_YET_FOUND; // 0=Auto and not found, -1 = Don't look for it, 1=Forced but not found, 2=Forced and found, 3=Found

/* MP4 related stuff */
unsigned mp4vidtrack=0; // Process the video track even if a CC dedicated track exists.

struct s_write wbout1, wbout2; // Output structures

/* File handles */
FILE *fh_out_elementarystream;
int infd=-1; // descriptor number to input. Set to -1 to indicate no file is open.
char *basefilename_for_stdin=(char *) "stdin"; // Default name for output files if input is stdin

int PIDs_seen[65536];

int temp_debug=0; // This is a convenience variable used to enable/disable debug on variable conditions. Find references to understand.

int main_telxcc (int argc, char *argv[]);

int main(int argc, char *argv[])
{
	char *c;

    // Initialize some constants
    init_ts_constants();

    // Prepare write structures
    init_write(&wbout1, 1);
    init_write(&wbout2, 2);

	// Init XDS buffers
	xds_init();
	//xds_cea608_test();
	
    // Prepare time structures
    init_boundary_time (&extraction_start);
    init_boundary_time (&extraction_end);
    init_boundary_time (&startcreditsnotbefore);
    init_boundary_time (&startcreditsnotafter);
    init_boundary_time (&startcreditsforatleast);
    init_boundary_time (&startcreditsforatmost);
    init_boundary_time (&endcreditsforatleast);
    init_boundary_time (&endcreditsforatmost);

    int show_myth_banner = 0;
    
	memset (&cea708services[0],0,63*sizeof (int));
    parse_parameters (argc,argv);

	if (num_input_files==0 && !input_is_stdin)
    {
        usage ();
        fatal (EXIT_NO_INPUT_FILES, "(This help screen was shown because there were no input files)\n");        
    }
    if (num_input_files>1 && live_stream)
    {
        fatal(EXIT_TOO_MANY_INPUT_FILES, "Live stream mode accepts only one input file.\n");        
    }
	// teletext page number out of range
	if ((tlt_config.page != 0) && ((tlt_config.page < 100) || (tlt_config.page > 899))) {
		fatal (EXIT_NOT_CLASSIFIED, "Teletext page number could not be lower than 100 or higher than 899\n");
	}

    if (output_filename!=NULL)
    {
        // Use the given output file name for the field specified by
        // the -1, -2 switch. If -12 is used, the filename is used for
        // field 1.
        if (extract==2)
            wbout2.filename=output_filename;
        else
            wbout1.filename=output_filename;
    }

    switch (write_format)
    {
        case OF_RAW:
            extension = ".raw";
            break;
        case OF_SRT:
            extension = ".srt";
            break;
        case OF_SAMI:
            extension = ".smi";
            break;
        case OF_SMPTETT:
            extension = ".ttml";
            break;
        case OF_TRANSCRIPT:
            extension = ".txt";
            break;
        case OF_RCWT:
            extension = ".bin";
            break;
		case OF_NULL:
			extension = "";
			break;
        default:
            fatal (EXIT_BUG_BUG, "write_format doesn't have any legal value, this is a bug.\n");            
    }
    params_dump();

	// default teletext page
	if (tlt_config.page > 0) {
		// dec to BCD, magazine pages numbers are in BCD (ETSI 300 706)
		tlt_config.page = ((tlt_config.page / 100) << 8) | (((tlt_config.page / 10) % 10) << 4) | (tlt_config.page % 10);
	}

    if (auto_stream==SM_MCPOODLESRAW && write_format==OF_RAW)
    {
        fatal (EXIT_INCOMPATIBLE_PARAMETERS, "-in=raw can only be used if the output is a subtitle file.\n");
    }
    if (auto_stream==SM_RCWT && write_format==OF_RCWT && output_filename==NULL)
    {
        fatal (EXIT_INCOMPATIBLE_PARAMETERS,
               "CCExtractor's binary format can only be used simultaneously for input and\noutput if the output file name is specified given with -o.\n");
    }

    buffer = (unsigned char *) malloc (BUFSIZE);
    subline = (unsigned char *) malloc (SUBLINESIZE);
    pesheaderbuf = (unsigned char *) malloc (188); // Never larger anyway

	if (!input_is_stdin)
		basefilename = (char *) malloc (strlen (inputfile[0])+1);
	else
		basefilename = (char *) malloc (strlen (basefilename_for_stdin));
	if (basefilename == NULL)
		fatal (EXIT_NOT_ENOUGH_MEMORY, "Not enough memory\n");        
	if (!input_is_stdin)
		strcpy (basefilename, inputfile[0]);
	else
		strcpy (basefilename, basefilename_for_stdin);
    for (c=basefilename+strlen (basefilename)-1; c>basefilename &&
        *c!='.'; c--) {;} // Get last .
    if (*c=='.')
        *c=0;

    if (wbout1.filename==NULL)
    {
        wbout1.filename = (char *) malloc (strlen (basefilename)+3+strlen (extension)); 
        wbout1.filename[0]=0;
    }
    if (wbout2.filename==NULL)
    {
        wbout2.filename = (char *) malloc (strlen (basefilename)+3+strlen (extension));
        wbout2.filename[0]=0;
    }
    if (buffer == NULL || pesheaderbuf==NULL ||
        wbout1.filename == NULL || wbout2.filename == NULL ||
        subline==NULL || init_file_buffer() || general_608_init())
    {
        fatal (EXIT_NOT_ENOUGH_MEMORY, "Not enough memory\n");        
    }

	if (write_format!=OF_NULL)
	{
		/* # DVD format uses one raw file for both fields, while Broadcast requires 2 */
		if (rawmode==1)
		{
			if (wbout1.filename[0]==0)
			{
				strcpy (wbout1.filename,basefilename);
				strcat (wbout1.filename,".raw");
			}
			if (cc_to_stdout)
			{
				wbout1.fh=STDOUT_FILENO;
				mprint ("Sending captions to stdout.\n");
			}
			else
			{
				mprint ("Creating %s\n", wbout1.filename);			
				wbout1.fh=open (wbout1.filename, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
				if (wbout1.fh==-1)
				{
					fatal (EXIT_FILE_CREATION_FAILED, "Failed\n");
				}
			}
		}
		else
		{
			if (cc_to_stdout && extract==12)			
				fatal (EXIT_INCOMPATIBLE_PARAMETERS, "You can't extract both fields to stdout at the same time in broadcast mode.");
			
			if (extract!=2)
			{
				if (cc_to_stdout)
				{
					wbout1.fh=STDOUT_FILENO;
					mprint ("Sending captions to stdout.\n");
				}
				else
				{
					if (wbout1.filename[0]==0)
					{
						strcpy (wbout1.filename,basefilename);
						if (extract==12) // _1 only added if there's two files
							strcat (wbout1.filename,"_1");
						strcat (wbout1.filename,(const char *) extension);
					}
					mprint ("Creating %s\n", wbout1.filename);				
					wbout1.fh=open (wbout1.filename, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
					if (wbout1.fh==-1)
					{
						fatal (EXIT_FILE_CREATION_FAILED, "Failed (errno=%d)\n",errno);
					}
				}
				if (write_format==OF_RAW)
					writeraw (BROADCAST_HEADER,sizeof (BROADCAST_HEADER),&wbout1);
				else
				{
					if (encoding==ENC_UNICODE) // Write BOM				
						writeraw (LITTLE_ENDIAN_BOM, sizeof (LITTLE_ENDIAN_BOM), &wbout1);
					write_subtitle_file_header (&wbout1);
				}

			}
			if (extract == 12) 
				mprint (" and \n");
			if (extract!=1)
			{
				if (cc_to_stdout)
				{
					wbout1.fh=STDOUT_FILENO;
					mprint ("Sending captions to stdout.\n");
				}
				else
				{
					if (wbout2.filename[0]==0)
					{
						strcpy (wbout2.filename,basefilename);				
						if (extract==12) // _ only added if there's two files
							strcat (wbout2.filename,"_2");
						strcat (wbout2.filename,(const char *) extension);
					}
					mprint ("Creating %s\n", wbout2.filename);
					wbout2.fh=open (wbout2.filename, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
					if (wbout2.fh==-1)
					{
						fatal (EXIT_FILE_CREATION_FAILED, "Failed\n");                
					}
				}
				if (write_format==OF_RAW)
					writeraw (BROADCAST_HEADER,sizeof (BROADCAST_HEADER),&wbout2);
				else
				{
					if (encoding==ENC_UNICODE) // Write BOM				
						writeraw (LITTLE_ENDIAN_BOM, sizeof (LITTLE_ENDIAN_BOM), &wbout1);
					write_subtitle_file_header (&wbout2);
				}
			}
		}
	}
    fh_out_elementarystream = NULL;
    if (out_elementarystream_filename!=NULL)
    {
        if ((fh_out_elementarystream = fopen (out_elementarystream_filename,"wb"))==NULL)
        {
            fatal (EXIT_FILE_CREATION_FAILED, "Unable to open clean file: %s\n",out_elementarystream_filename);
        }
    }
    encoded_crlf_length = encode_line (encoded_crlf,(unsigned char *) "\r\n"); 
    encoded_br_length = encode_line (encoded_br, (unsigned char *) "<br>");
    build_parity_table();

    // Initialize HDTV caption buffer
    init_hdcc();
    init_708(); // Init 708 decoders

    time_t start, final;
    time(&start);

    processed_enough=0;
    if (binary_concat)
    {
        total_inputsize=gettotalfilessize();
        if (total_inputsize==-1)
            fatal (EXIT_UNABLE_TO_DETERMINE_FILE_SIZE, "Failed to determine total file size.\n");
    }

    while (switch_to_next_file(0) && !processed_enough)
    {
        prepare_for_new_file();

        if (auto_stream == SM_AUTODETECT)
        {
            detect_stream_type();            
            switch (stream_mode)
            {
                case SM_ELEMENTARY_OR_NOT_FOUND:
                    mprint ("\rFile seems to be an elementary stream, enabling ES mode\n");
                    break;
                case SM_TRANSPORT:
                    mprint ("\rFile seems to be a transport stream, enabling TS mode\n");
                    break;
                case SM_PROGRAM:
                    mprint ("\rFile seems to be a program stream, enabling PS mode\n");
                    break;
                case SM_ASF:
                    mprint ("\rFile seems to be an ASF, enabling DVR-MS mode\n");
                    break;
                case SM_MCPOODLESRAW:
                    mprint ("\rFile seems to be McPoodle raw data\n");
                    break;
                case SM_RCWT:
                    mprint ("\rFile seems to be a raw caption with time data\n");
                    break;
				case SM_MP4:
                    mprint ("\rFile seems to be a MP4\n");
                    break;
                case SM_MYTH:
                case SM_AUTODETECT:
                    fatal(EXIT_BUG_BUG, "Cannot be reached!");
                    break;
            }
        }
        else
        {
            stream_mode=auto_stream;
        }
	
        /* -----------------------------------------------------------------
        MAIN LOOP
        ----------------------------------------------------------------- */

        // The myth loop autodetect will only be used with ES or PS streams
        switch (auto_myth)
        {
            case 0:
                // Use whatever stream mode says
                break;
            case 1:
                // Force stream mode to myth
                stream_mode=SM_MYTH;
                break;
            case 2:
                // autodetect myth files, but only if it does not conflict with
                // the current stream mode
                switch (stream_mode)
                {
                    case SM_ELEMENTARY_OR_NOT_FOUND:
                    case SM_PROGRAM:
                        if ( detect_myth() )
                        {
                            stream_mode=SM_MYTH;
                        }
                        break;
                    default:
                        // Keep stream_mode
                        break;
                }
                break;                    
        }
                
        switch (stream_mode)
        {
            case SM_ELEMENTARY_OR_NOT_FOUND:
                use_gop_as_pts = 1; // Force GOP timing for ES
            case SM_TRANSPORT:
            case SM_PROGRAM:
            case SM_ASF:
                mprint ("\rAnalyzing data in general mode\n");
                general_loop();
                break;
            case SM_MCPOODLESRAW:
                mprint ("\rAnalyzing data in McPoodle raw mode\n");
                raw_loop();
                break;
            case SM_RCWT:
                mprint ("\rAnalyzing data in CCExtractor's binary format\n");
                rcwt_loop();
                break;
            case SM_MYTH:
                mprint ("\rAnalyzing data in MythTV mode\n");
                show_myth_banner = 1;
                myth_loop();
				break;
			case SM_MP4:				
                mprint ("\rAnalyzing data with GPAC (MP4 library)\n");
				close_input_file(); // No need to have it open. GPAC will do it for us
				processmp4 (inputfile[0]);										
				break;
            case SM_AUTODETECT:
                fatal(EXIT_BUG_BUG, "Cannot be reached!");
                break;
        }

        mprint("\n");
        dbg_print(DMT_608, "\nTime stamps after last caption block was written:\n");
        dbg_print(DMT_608, "Last time stamps:  PTS: %s (%+2dF)        ",
               print_mstime( LLONG(sync_pts/(MPEG_CLOCK_FREQ/1000)
                                   +frames_since_ref_time*1000.0/current_fps) ),
               frames_since_ref_time);
        dbg_print(DMT_608, "GOP: %s      \n", print_mstime(gop_time.ms) );

        // Blocks since last PTS/GOP time stamp.
        dbg_print(DMT_608, "Calc. difference:  PTS: %s (%+3lldms incl.)  ",
            print_mstime( LLONG((sync_pts-min_pts)/(MPEG_CLOCK_FREQ/1000)
            + fts_offset + frames_since_ref_time*1000.0/current_fps)),
            fts_offset + LLONG(frames_since_ref_time*1000.0/current_fps) );
        dbg_print(DMT_608, "GOP: %s (%+3dms incl.)\n",
            print_mstime((LLONG)(gop_time.ms
            -first_gop_time.ms
            +get_fts_max()-fts_at_gop_start)),
            (int)(get_fts_max()-fts_at_gop_start));
        // When padding is active the CC block time should be within
        // 1000/29.97 us of the differences.
        dbg_print(DMT_608, "Max. FTS:       %s  (without caption blocks since then)\n",
            print_mstime(get_fts_max()));

        if (stat_hdtv)
        {
            mprint ("\rCC type 0: %d (%s)\n", cc_stats[0], cc_types[0]);
            mprint ("CC type 1: %d (%s)\n", cc_stats[1], cc_types[1]);
            mprint ("CC type 2: %d (%s)\n", cc_stats[2], cc_types[2]);
            mprint ("CC type 3: %d (%s)\n", cc_stats[3], cc_types[3]);
        }
        mprint ("\nTotal frames time:      %s  (%u frames at %.2ffps)\n",
            print_mstime( (LLONG)(total_frames_count*1000/current_fps) ),
            total_frames_count, current_fps);
        if (total_pulldownframes)
            mprint ("incl. pulldown frames:  %s  (%u frames at %.2ffps)\n",
                    print_mstime( (LLONG)(total_pulldownframes*1000/current_fps) ),
                    total_pulldownframes, current_fps);
        if (pts_set >= 1 && min_pts != 0x01FFFFFFFFLL)
        {
            LLONG postsyncms = LLONG(frames_since_last_gop*1000/current_fps);
            mprint ("\nMin PTS:                %s\n",
                    print_mstime( min_pts/(MPEG_CLOCK_FREQ/1000) - fts_offset));
            if (pts_big_change)
                mprint ("(Reference clock was reset at some point, Min PTS is approximated)\n");
            mprint ("Max PTS:                %s\n",
                    print_mstime( sync_pts/(MPEG_CLOCK_FREQ/1000) + postsyncms));

            mprint ("Length:                 %s\n",
                    print_mstime( sync_pts/(MPEG_CLOCK_FREQ/1000) + postsyncms
                                  - min_pts/(MPEG_CLOCK_FREQ/1000) + fts_offset ));
        }
        // dvr-ms files have invalid GOPs
        if (gop_time.inited && first_gop_time.inited && stream_mode != SM_ASF)
        {
            mprint ("\nInitial GOP time:       %s\n",
                print_mstime(first_gop_time.ms));
            mprint ("Final GOP time:         %s%+3dF\n",
                print_mstime(gop_time.ms),
                frames_since_last_gop);
            mprint ("Diff. GOP length:       %s%+3dF",
                print_mstime(gop_time.ms - first_gop_time.ms),
                frames_since_last_gop);
            mprint ("    (%s)\n",
                print_mstime(gop_time.ms - first_gop_time.ms
                +LLONG((frames_since_last_gop)*1000/29.97)) );
        }

        if (false_pict_header)
            mprint ("\nNumber of likely false picture headers (discarded): %d\n",false_pict_header);

        if (stat_numuserheaders)
            mprint("\nTotal user data fields: %d\n", stat_numuserheaders);
        if (stat_dvdccheaders)
            mprint("DVD-type user data fields: %d\n", stat_dvdccheaders);
        if (stat_scte20ccheaders)
            mprint("SCTE-20 type user data fields: %d\n", stat_scte20ccheaders);
        if (stat_replay4000headers)
            mprint("ReplayTV 4000 user data fields: %d\n", stat_replay4000headers);
        if (stat_replay5000headers)
            mprint("ReplayTV 5000 user data fields: %d\n", stat_replay5000headers);
        if (stat_hdtv)
            mprint("HDTV type user data fields: %d\n", stat_hdtv);
        if (stat_dishheaders)
            mprint("Dish Network user data fields: %d\n", stat_dishheaders);
        if (stat_divicom)
        {
            mprint("CEA608/Divicom user data fields: %d\n", stat_divicom);

            mprint("\n\nNOTE! The CEA 608 / Divicom standard encoding for closed\n");
            mprint("caption is not well understood!\n\n");
            mprint("Please submit samples to the developers.\n\n\n");
        }

        // Add one frame as fts_max marks the beginning of the last frame,
        // but we need the end.
        fts_global += fts_max + LLONG(1000.0/current_fps);
		// CFS: At least in Hauppage mode, cb_field can be responsible for ALL the 
		// timing (cb_fields having a huge number and fts_now and fts_global being 0 all
		// the time), so we need to take that into account in fts_global before resetting
		// counters.
		if (cb_field1!=0)
			fts_global += cb_field1*1001/3;
		else
			fts_global += cb_field2*1001/3;
        // Reset counters - This is needed if some captions are still buffered
        // and need to be written after the last file is processed.		
        cb_field1 = 0; cb_field2 = 0; cb_708 = 0;
        fts_now = 0;
        fts_max = 0;        
    } // file loop
    close_input_file();
    
    if (fh_out_elementarystream!=NULL)
        fclose (fh_out_elementarystream);	

    flushbuffer (&wbout1,false);
    flushbuffer (&wbout2,false);

    prepare_for_new_file (); // To reset counters used by handle_end_of_data()

    if (wbout1.fh!=-1)
    {
        if (write_format==OF_SMPTETT || write_format==OF_SAMI || write_format==OF_SRT || write_format==OF_TRANSCRIPT)
        {
            handle_end_of_data (&wbout1);
        }
        else if(write_format==OF_RCWT)
        {
            // Write last header and data
            writercwtdata (NULL);
        }
        if (end_credits_text!=NULL)
            try_to_add_end_credits(&wbout1);
        write_subtitle_file_footer (&wbout1);
    }
    if (wbout2.fh!=-1)
    {
        if (write_format==OF_SMPTETT || write_format==OF_SAMI || write_format==OF_SRT || write_format==OF_TRANSCRIPT)
        {
            handle_end_of_data (&wbout2);
        }
        if (end_credits_text!=NULL)
            try_to_add_end_credits(&wbout2);
        write_subtitle_file_footer (&wbout2);
    }
	telxcc_close();
    flushbuffer (&wbout1,true);
    flushbuffer (&wbout2,true);
    time (&final);

    long proc_time=(long) (final-start);
    mprint ("\rDone, processing time = %ld seconds\n", proc_time);
    if (proc_time>0)
    {
        LLONG ratio=(get_fts_max()/10)/proc_time;
        unsigned s1=(unsigned) (ratio/100);
        unsigned s2=(unsigned) (ratio%100);    
        mprint ("Performance (real length/process time) = %u.%02u\n", 
            s1, s2);
    }
    dbg_print(DMT_708, "The 708 decoder was reset [%d] times.\n",resets_708);
	if (telext_mode == TXT_IN_USE)
		mprint ( "Teletext decoder: %"PRIu32" packets processed, %"PRIu32" SRT frames written.\n", tlt_packet_counter, tlt_frames_produced);

    if (processed_enough)
    {
        mprint ("\rNote: Processing was cancelled before all data was processed because\n");
        mprint ("\rone or more user-defined limits were reached.\n");
    } 
	if (ccblocks_in_avc_lost>0)
	{
		mprint ("Total caption blocks received: %d\n", ccblocks_in_avc_total);
		mprint ("Total caption blocks lost: %d\n", ccblocks_in_avc_lost);
	}

    mprint ("This is beta software. Report issues to cfsmp3 at gmail...\n");
    if (show_myth_banner)
    {
        mprint ("NOTICE: Due to the major rework in 0.49, we needed to change part of the timing\n");
        mprint ("code in the MythTV's branch. Please report results to the address above. If\n");
        mprint ("something is broken it will be fixed. Thanks\n");        
    }
    return EXIT_OK;
}

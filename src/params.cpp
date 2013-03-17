#include "ccextractor.h"

int inputfile_capacity=0; 
int spell_builtin_added=0; // so we don't do it twice

const char *DEF_VAL_STARTCREDITSNOTBEFORE="0";
const char *DEF_VAL_STARTCREDITSNOTAFTER="5:00"; // To catch the theme after the teaser in TV shows
const char *DEF_VAL_STARTCREDITSFORATLEAST="2";
const char *DEF_VAL_STARTCREDITSFORATMOST="5";
const char *DEF_VAL_ENDCREDITSFORATLEAST="2";
const char *DEF_VAL_ENDCREDITSFORATMOST="5";

// Some basic English words, so user-defined doesn't have to
// include the common stuff
const char *spell_builtin[]=
{
    "I", "I'd",	"I've",	"I'd", "I'll",
    "January","February","March","April", // May skipped intentionally
    "June","July","August","September","October","November",
    "December","Monday","Tuesday","Wednesday","Thursday",
    "Friday","Saturday","Sunday","Halloween","United States",
    "Spain","France","Italy","England",
    NULL
}; 

int stringztoms (const char *s, boundary_time *bt)
{
    unsigned ss=0, mm=0, hh=0;
    int value=-1;
    int colons=0;
    const char *c=s;
    while (*c)
    {
        if (*c==':')
        {
            if (value==-1) // : at the start, or ::, etc
                return -1;
            colons++;
            if (colons>2) // Max 2, for HH:MM:SS
                return -1;
            hh=mm;
            mm=ss;
            ss=value;
            value=-1;			
        }
        else
        {
            if (!isdigit (*c)) // Only : or digits, so error
                return -1;
            if (value==-1)
                value=*c-'0';
            else
                value=value*10+*c-'0';
        }
        c++;
    }
    hh=mm;
    mm=ss;
    ss=value;
    if (mm>59 || ss>59)
        return -1;
    bt->set=1;
    bt->hh=hh;
    bt->mm=mm;
    bt->ss=ss;	
    LLONG secs=(hh*3600+mm*60+ss);
    bt->time_in_ms=secs*1000;
    return 0;
}


int add_word (const char *word)
{
    char *new_lower;
    char *new_correct;
    if (spell_words==spell_capacity)
    {
        // Time to grow
        spell_capacity+=50;
        spell_lower=(char **) realloc (spell_lower, sizeof (char *) * 
            spell_capacity);
        spell_correct=(char **) realloc (spell_correct, sizeof (char *) * 
            spell_capacity);		
    }
    size_t len=strlen (word);
    new_lower = (char *) malloc (len+1);
    new_correct = (char *) malloc (len+1);
    if (spell_lower==NULL || spell_correct==NULL ||
        new_lower==NULL || new_correct==NULL)
    {        
        return -1;
    }
    strcpy (new_correct, word);
    for (size_t i=0; i<len; i++)
    {
        char c=new_correct[i];
        c=tolower (c); // TO-DO: Add Spanish characters
        new_lower[i]=c;
    }
    new_lower[len]=0;
    spell_lower[spell_words]=new_lower;
    spell_correct[spell_words]=new_correct;
    spell_words++;
    return 0;
}


int add_built_in_words()
{
    if (!spell_builtin_added)
    {
        int i=0;
        while (spell_builtin[i]!=NULL)
        {
            if (add_word(spell_builtin[i]))
                return -1;
            i++;
        }
        spell_builtin_added=1;
    }
    return 0;
}


int process_cap_file (char *filename)
{
    FILE *fi = fopen (filename,"rt");
    if (fi==NULL)
    {
        mprint ("\rUnable to open capitalization file: %s\n", filename);
        return -1;
    }
    char line[35]; // For screen width (32)+CRLF+0
    int num=0;
    while (fgets (line,35,fi))
    {
        num++;
        if (line[0]=='#') // Comment
            continue;
        char *c=line+strlen (line)-1;
        while (c>=line && (*c==0xd || *c==0xa))
        {
            *c=0;
            c--;
        }
        if (strlen (line)>32)
        {
            mprint ("Word in line %d too long, max = 32 characters.\n",num);
            fclose (fi);
            return -1;
        }
        if (strlen (line)>0)
        {
            if (add_word (line))
                return -1;
        }
    }
    fclose (fi);
    return 0;
}

int isanumber (char *s)
{
    while (*s)
    {
        if (!isdigit (*s))
            return 0;
        s++;
    }
    return 1;
}

int parsedelay (char *par)
{
    int sign=0; 
    char *c=par;
    while (*c)
    {		
        if (*c=='-' || *c=='+')
        {
            if (c!=par) // Sign only at the beginning
                return 1; 
            if (*c=='-')
                sign=1;
        }
        else
        {
            if (!isdigit (*c))
                return 1;
            subs_delay=subs_delay*10 + (*c-'0');			
        }
        c++;
    }
    if (sign)
        subs_delay=-subs_delay;
    return 0;
}

int append_file_to_queue (char *filename)
{
    char *c=(char *) malloc (strlen (filename)+1);    
    if (c==NULL)
        return -1;           
    strcpy (c,filename);
    if (inputfile_capacity<=num_input_files)
    {
        inputfile_capacity+=10;
        inputfile=(char **) realloc (inputfile,sizeof (char *) * inputfile_capacity);
        if (inputfile==NULL)
            return -1;
    }
    inputfile[num_input_files]=c;
    num_input_files++;            
    return 0;
}

int add_file_sequence (char *filename)
{
    int m,n;
    n=strlen (filename)-1;
    // Look for the last digit in filename
    while (n>=0 && !isdigit (filename[n]))
        n--;
    if (n==-1) // None. No expansion needed    
        return append_file_to_queue(filename);
    m=n;
    while (m>=0 && isdigit (filename[m]))
        m--;
    m++;
    // Here: Significant digits go from filename[m] to filename[n]
    char *num=(char *) malloc (n-m+2);
    strncpy (num,filename+m, n-m+1);
    num[n-m+1]=0;
    int i = atoi (num);
    char *temp=(char *) malloc (n-m+3); // For overflows
    // printf ("Expanding %d to %d, initial value=%d\n",m,n,i);
    for (;;)
    {
        FILE *f=fopen (filename,"r");
        if (f==NULL) // Doesn't exist or we can't read it. We're done
            break;
        fclose (f);
        if (append_file_to_queue (filename)) // Memory panic
            return -1;
        i++;
        sprintf (temp,"%d",i);
        if (strlen (temp)>strlen (num)) // From 999 to 1000, etc.
            break;
        strncpy (filename+m+(strlen (num)-strlen (temp)),temp,strlen (temp));
        memset (filename+m,'0',strlen (num)-strlen (temp));        
    }
    return 0;
}

void set_output_format (const char *format)
{
    while (*format=='-')
        format++;
    if (strcmp (format,"srt")==0)
        write_format=OF_SRT;
    else if (strcmp (format,"sami")==0 || strcmp (format,"smi")==0) 
        write_format=OF_SAMI;
    else if (strcmp (format,"transcript")==0 || strcmp (format,"txt")==0)
	{
        write_format=OF_TRANSCRIPT;
		timestamps_on_transcript=0;
	}
    else if (strcmp (format,"timedtranscript")==0 || strcmp (format,"ttxt")==0)
	{
        write_format=OF_TRANSCRIPT;
		timestamps_on_transcript=1;
	}
    else if (strcmp (format,"raw")==0)
        write_format=OF_RAW;
    else if (strcmp (format,"bin")==0)
        write_format=OF_RCWT;
    else if (strcmp (format,"null")==0)
        write_format=OF_NULL;
    else if (strcmp (format,"dvdraw")==0)
    {
        write_format=OF_RAW;
        rawmode=1;
    }
    else if (strcmp (format, "smptett")==0)
      write_format=OF_SMPTETT ;
    else
        fatal (EXIT_MALFORMED_PARAMETER, "Unknown output file format: %s\n", format);
}

void set_input_format (const char *format)
{
    while (*format=='-')
        format++;
    if (strcmp (format,"es")==0) // Does this actually do anything?
        auto_stream = SM_ELEMENTARY_OR_NOT_FOUND;
    else if (strcmp (format,"ts")==0)
        auto_stream = SM_TRANSPORT;
    else if (strcmp (format,"ps")==0 || strcmp (format,"nots")==0)
        auto_stream = SM_PROGRAM;
    else if (strcmp (format,"asf")==0 || strcmp (format,"dvr-ms")==0)
        auto_stream = SM_ASF;
    else if (strcmp (format,"raw")==0)
        auto_stream = SM_MCPOODLESRAW;
    else if (strcmp (format,"bin")==0)
        auto_stream = SM_RCWT;
    else if (strcmp (format,"mp4")==0)
        auto_stream = SM_MP4;
    else
        fatal (EXIT_MALFORMED_PARAMETER, "Unknown input file format: %s\n", format);
}

void usage (void)
{
    mprint ("Originally based on McPoodle's tools. Check his page for lots of information\n");
    mprint ("on closed captions technical details.\n");
    mprint ("(http://www.geocities.com/mcpoodle43/SCC_TOOLS/DOCS/SCC_TOOLS.HTML)\n\n");
    mprint ("This tool home page:\n");
    mprint ("http://ccextractor.sourceforge.net\n");
    mprint ("  Extracts closed captions from MPEG files.\n");
    mprint ("    (DVB, .TS, ReplayTV 4000 and 5000, dvr-ms, bttv, Tivo and Dish Network\n");
    mprint ("      are known to work).\n\n");
    mprint ("  Syntax:\n");
    mprint ("  ccextractor [options] inputfile1 [inputfile2...] [-o outputfilename]\n");
    mprint ("               [-o1 outputfilename1] [-o2 outputfilename2]\n\n");
    mprint ("File name related options:\n");
    mprint ("            inputfile: file(s) to process\n");
    mprint ("    -o outputfilename: Use -o parameters to define output filename if you don't\n");
    mprint ("                       like the default ones (same as infile plus _1 or _2 when\n");
    mprint ("                       needed and .raw or .srt extension).\n");
    mprint ("                           -o or -o1 -> Name of the first (maybe only) output\n");
    mprint ("                                        file.\n");
    mprint ("                           -o2       -> Name of the second output file, when\n");
    mprint ("                                        it applies.\n");
    mprint ("         -cf filename: Write 'clean' data to a file. Cleans means the ES\n");
    mprint ("                       without TS or PES headers.\n");
	mprint ("              -stdout: Write output to stdout (console) instead of file. If\n");
	mprint ("                       stdout is used, then -o, -o1 and -o2 can't be used. Also\n");
	mprint ("                       -stdout will redirect all messages to stderr (error).\n\n");
    mprint ("You can pass as many input files as you need. They will be processed in order.\n");
    mprint ("If a file name is suffixed by +, ccextractor will try to follow a numerical\n");
    mprint ("sequence. For example, DVD001.VOB+ means DVD001.VOB, DVD002.VOB and so on\n");
    mprint ("until there are no more files.\n");
    mprint ("Output will be one single file (either raw or srt). Use this if you made your\n");
    mprint ("recording in several cuts (to skip commercials for example) but you want one\n");
    mprint ("subtitle file with contiguous timing.\n\n");
    mprint ("Options that affect what will be processed:\n");
    mprint ("          -1, -2, -12: Output Field 1 data, Field 2 data, or both\n");
    mprint ("                       (DEFAULT is -1)\n");
    mprint ("                 -cc2: When in srt/sami mode, process captions in channel 2\n");
    mprint ("                       instead channel 1.\n\n");
	mprint ("-svc --service N,N...: Enabled CEA-708 captions processing for the listed\n");
	mprint ("                       services. The parameter is a command delimited list\n");
	mprint ("                       of services numbers, such as \"1,2\" to process the\n");
	mprint ("                       primary and secondary language services.\n");
    mprint ("In general, if you want English subtitles you don't need to use these options\n");
    mprint ("as they are broadcast in field 1, channel 1. If you want the second language\n");
    mprint ("(usually Spanish) you may need to try -2, or -cc2, or both.\n\n");
    mprint ("Input formats:\n");
    mprint ("       With the exception of McPoodle's raw format, which is just the closed\n");
    mprint ("       caption data with no other info, CCExtractor can usually detect the\n");
    mprint ("       input format correctly. To force a specific format:\n\n");
    mprint ("                  -in=format\n\n");
    mprint ("       where format is one of these:\n");
    mprint ("                       ts   -> For Transport Streams.\n");
    mprint ("                       ps   -> For Program Streams.\n");
    mprint ("                       es   -> For Elementary Streams.\n");
    mprint ("                       asf  -> ASF container (such as DVR-MS).\n");
    mprint ("                       bin  -> CCExtractor's own binary format.\n\n");
    mprint ("                       raw  -> For McPoodle's raw files.\n\n");
	mprint ("                       mp4  -> MP4/MOV/M4V and similar.\n\n");
    mprint ("       -ts, -ps, -es, -mp4 and -asf (or --dvr-ms) can be used as shorts.\n\n");
    mprint ("Output formats:\n\n");
    mprint ("                 -out=format\n\n");
    mprint ("       where format is one of these:\n");
    mprint ("                       srt    -> SubRip (default, so not actually needed).\n");
    mprint ("                       sami   -> MS Synchronized Accesible Media Interface.\n");
    mprint ("                       bin    -> CC data in CCExtractor's own binary format.\n");
    mprint ("                       raw    -> CC data in McPoodle's Broadcast format.\n");
    mprint ("                       dvdraw -> CC data in McPoodle's DVD format.\n");
    mprint ("                       txt    -> Transcript (no time codes, no roll-up\n");
    mprint ("                                 captions, just the plain transcription.\n\n");
    mprint ("                       ttxt   -> Timed Transcript (transcription with time info)\n");
	mprint ("                       null   -> Don't produce any file output\n\n");
     
    mprint ("Options that affect how input files will be processed.\n");
   
    mprint ("        -gt --goptime: Use GOP for timing instead of PTS. This only applies\n");
    mprint ("                       to Program or Transport Streams with MPEG2 data and\n");
    mprint ("                       overrides the default PTS timing.\n");
    mprint ("                       GOP timing is always used for Elementary Streams.\n");
    mprint ("     -fp --fixpadding: Fix padding - some cards (or providers, or whatever)\n");
    mprint ("                       seem to send 0000 as CC padding instead of 8080. If you\n");
    mprint ("                       get bad timing, this might solve it.\n");
    mprint ("               -90090: Use 90090 (instead of 90000) as MPEG clock frequency.\n");
    mprint ("                       (reported to be needed at least by Panasonic DMR-ES15\n");
    mprint ("                       DVD Recorder)\n");
    mprint ("    -ve --videoedited: By default, ccextractor will process input files in\n");
    mprint ("                       sequence as if they were all one large file (i.e.\n");
    mprint ("                       split by a generic, non video-aware tool. If you\n");
    mprint ("                       are processing video hat was split with a editing\n");
    mprint ("                       tool, use -ve so ccextractor doesn't try to rebuild\n");
    mprint ("                       the original timing.\n");
    mprint ("   -s --stream [secs]: Consider the file as a continuous stream that is\n");
    mprint ("                       growing as ccextractor processes it, so don't try\n");
    mprint ("                       to figure out its size and don't terminate processing\n");
    mprint ("                       when reaching the current end (i.e. wait for more\n");
    mprint ("                       data to arrive). If the optional parameter secs is\n");
    mprint ("                       present, it means the number of seconds without any\n");
    mprint ("                       new data after which ccextractor should exit. Use\n");
    mprint ("                       this parameter if you want to process a live stream\n");
    mprint ("                       but not kill ccextractor externally.\n");
    mprint ("                       Note: If -s is used then only one input file is\n");
    mprint ("                       allowed.\n");
    mprint ("  -poc  --usepicorder: Use the pic_order_cnt_lsb in AVC/H.264 data streams\n");
    mprint ("                       to order the CC information.  The default way is to\n");
    mprint ("                       use the PTS information.  Use this switch only when\n");
    mprint ("                       needed.\n");
    mprint ("                -myth: Force MythTV code branch.\n");
    mprint ("              -nomyth: Disable MythTV code branch.\n");
    mprint ("                       The MythTV branch is needed for analog captures where\n");
    mprint ("                       the closed caption data is stored in the VBI, such as\n");
    mprint ("                       those with bttv cards (Hauppage 250 for example). This is\n");
    mprint ("                       detected automatically so you don't need to worry about\n");
    mprint ("                       this unless autodetection doesn't work for you.\n");
    mprint ("       -wtvconvertfix: This switch works around a bug in Windows 7's built in\n");
    mprint ("                       software to convert *.wtv to *.dvr-ms. For analog NTSC\n");
    mprint ("                       recordings the CC information is marked as digital\n");
    mprint ("                       captions. Use this switch only when needed.\n");
	mprint (" -pn --program-number: In TS mode, specifically select a program to process.\n");
	mprint ("                       Not needed if the TS only has one. If this parameter\n");
	mprint ("                       is not specified and CCExtractor detects more than one\n");
	mprint ("                       program in the input, it will list the programs found\n");
	mprint ("                       and terminate without doing anything.\n");
	mprint ("    -haup --hauppauge: If the video was recorder using a Hauppauge card, it might\n");
	mprint ("                       need special processing. This parameter will force\n");
	mprint ("                       the special treatment.\n");
	mprint ("         -mp4vidtrack: In MP4 files the closed caption data can be embedded in the\n");
	mprint ("                       video track or in a dedicated CC track. If a dedicated track\n");
	mprint ("                       is detected it will be processed instead of the video track.\n");
	mprint ("                       If you need to force the video track to be processed instead\n");
	mprint ("                       use this option.\n");
    mprint ("\n");
    mprint ("Options that affect what kind of output will be produced:\n");
    mprint ("             -unicode: Encode subtitles in Unicode instead of Latin-1\n");
    mprint ("                -utf8: Encode subtitles in UTF-8 instead of Latin-1\n");    
    mprint ("  -nofc --nofontcolor: For .srt/.sami, don't add font color tags.\n");
	mprint ("-nots --notypesetting: For .srt/.sami, don't add typesetting tags.\n");
    mprint ("                -trim: Trim lines.\n");
    mprint ("   -dc --defaultcolor: Select a different default color (instead of\n");
    mprint ("                       white). This causes all output in .srt/.smi\n");
    mprint ("                       files to have a font tag, which makes the files\n");
    mprint ("                       larger. Add the color you want in RGB, such as\n");
    mprint ("                       -dc #FF0000 for red.\n");
    mprint ("    -sc --sentencecap: Sentence capitalization. Use if you hate\n");
    mprint ("                       ALL CAPS in subtitles.\n");
    mprint ("  --capfile -caf file: Add the contents of 'file' to the list of words\n");
    mprint ("                       that must be capitalized. For example, if file\n");
    mprint ("                       is a plain text file that contains\n\n");
    mprint ("                       Tony\n");
    mprint ("                       Alan\n\n");
    mprint ("                       Whenever those words are found they will be written\n");
    mprint ("                       exactly as they appear in the file.\n");
    mprint ("                       Use one line per word. Lines starting with # are\n");
    mprint ("                       considered comments and discarded.\n\n");
    mprint ("Options that affect how ccextractor reads and writes (buffering):\n");
    mprint ("    -bi --bufferinput: Forces input buffering.\n");    
    mprint (" -nobi -nobufferinput: Disables input buffering.\n\n");    
    mprint ("Note: -bo is only used when writing raw files, not .srt or .sami\n\n");
    mprint ("Options that affect the built-in closed caption decoder:\n");
    mprint ("                 -dru: Direct Roll-Up. When in roll-up mode, write character by\n");
    mprint ("                       character instead of line by line. Note that this\n");
    mprint ("                       produces (much) larger files.\n");
    mprint ("     -noru --norollup: If you hate the repeated lines caused by the roll-up\n");
    mprint ("                       emulation, you can have ccextractor write only one\n");
    mprint ("                       line at a time, getting rid of these repeated lines.\n\n");
    mprint ("Options that affect timing:\n");
    mprint ("            -delay ms: For srt/sami, add this number of milliseconds to\n");
    mprint ("                       all times. For example, -delay 400 makes subtitles\n");
    mprint ("                       appear 400ms late. You can also use negative numbers\n");
    mprint ("                       to make subs appear early.\n");
    mprint ("Notes on times: -startat and -endat times are used first, then -delay.\n");
    mprint ("So if you use -srt -startat 3:00 -endat 5:00 -delay 120000, ccextractor will\n");
    mprint ("generate a .srt file, with only data from 3:00 to 5:00 in the input file(s)\n");
    mprint ("and then add that (huge) delay, which would make the final file start at\n");
    mprint ("5:00 and end at 7:00.\n\n");
    mprint ("Options that affect what segment of the input file(s) to process:\n");
    mprint ("        -startat time: Only write caption information that starts after the\n");
    mprint ("                       given time.\n");
    mprint ("                       Time can be seconds, MM:SS or HH:MM:SS.\n");
    mprint ("                       For example, -startat 3:00 means 'start writing from\n");
    mprint ("                       minute 3.\n");
    mprint ("          -endat time: Stop processing after the given time (same format as\n");
    mprint ("                       -startat).\n");
    mprint ("                       The -startat and -endat options are honored in all\n");
    mprint ("                       output formats.  In all formats with timing information\n");
    mprint ("                       the times are unchanged.\n");
    mprint ("-scr --screenfuls num: Write 'num' screenfuls and terminate processing.\n\n");
    mprint ("Adding start and end credits:\n");
    mprint ("  CCExtractor can _try_ to add a custom message (for credits for example) at\n");
    mprint ("  the start and end of the file, looking for a window where there are no\n");
    mprint ("  captions. If there is no such window, then no text will be added.\n");
    mprint ("  The start window must be between the times given and must have enough time\n");
    mprint ("  to display the message for at least the specified time.\n");
    mprint ("        --startcreditstext txt: Write this text as start credits. If there are\n");
    mprint ("                                several lines, separate them with the\n");
    mprint ("                                characters \\n, for example Line1\\nLine 2.\n");
    mprint ("  --startcreditsnotbefore time: Don't display the start credits before this\n");
    mprint ("                                time (S, or MM:SS). Default: %s\n", DEF_VAL_STARTCREDITSNOTBEFORE);
    mprint ("   --startcreditsnotafter time: Don't display the start credits after this\n");
    mprint ("                                time (S, or MM:SS). Default: %s\n", DEF_VAL_STARTCREDITSNOTAFTER);
    mprint (" --startcreditsforatleast time: Start credits need to be displayed for at least\n");
    mprint ("                                this time (S, or MM:SS). Default: %s\n", DEF_VAL_STARTCREDITSFORATLEAST);
    mprint ("  --startcreditsforatmost time: Start credits should be displayed for at most\n");
    mprint ("                                this time (S, or MM:SS). Default: %s\n", DEF_VAL_STARTCREDITSFORATMOST);
    mprint ("          --endcreditstext txt: Write this text as end credits. If there are\n");
    mprint ("                                several lines, separate them with the\n");
    mprint ("                                characters \\n, for example Line1\\nLine 2.\n");
    mprint ("   --endcreditsforatleast time: End credits need to be displayed for at least\n");
    mprint ("                                this time (S, or MM:SS). Default: %s\n", DEF_VAL_ENDCREDITSFORATLEAST);
    mprint ("    --endcreditsforatmost time: End credits should be displayed for at most\n");
    mprint ("                                this time (S, or MM:SS). Default: %s\n", DEF_VAL_ENDCREDITSFORATMOST);
    mprint ("\n");
    mprint ("Options that affect debug data:\n");
    mprint ("               -debug: Show lots of debugging output.\n");
    mprint ("                 -608: Print debug traces from the EIA-608 decoder.\n");
    mprint ("                       If you need to submit a bug report, please send\n");
    mprint ("                       the output from this option.\n");
    mprint ("                 -708: Print debug information from the (currently\n");
    mprint ("                       in development) EIA-708 (DTV) decoder.\n");
    mprint ("              -goppts: Enable lots of time stamp output.\n");
	mprint ("            -xdsdebug: Enable XDS debug data (lots of it).\n");
    mprint ("               -vides: Print debug info about the analysed elementary\n");
    mprint ("                       video stream.\n");
    mprint ("               -cbraw: Print debug trace with the raw 608/708 data with\n");
    mprint ("                       time stamps.\n");
    mprint ("              -nosync: Disable the syncing code.  Only useful for debugging\n");
    mprint ("                       purposes.\n");
    mprint ("             -fullbin: Disable the removal of trailing padding blocks\n");
    mprint ("                       when exporting to bin format.  Only useful for\n");
    mprint ("                       for debugging purposes.\n");
    mprint ("          -parsedebug: Print debug info about the parsed container\n");
    mprint ("                       file. (Only for TS/ASF files at the moment.)\n\n");
	mprint ("Teletext related options:\n");
	mprint ("          -tpage page: Use this page for subtitles (if this parameter\n");
	mprint ("                       is not used, try to autodetect). In Spain the\n");
	mprint ("                       page is always 888, may vary in other countries.\n");
	mprint ("            -tverbose: Enable verbose mode in the teletext decoder.\n\n");
    mprint ("Communication with other programs and console output:\n");
    mprint ("   --gui_mode_reports: Report progress and interesting events to stderr\n");
    mprint ("                       in a easy to parse format. This is intended to be\n");
    mprint ("                       used by other programs. See docs directory for.\n");
    mprint ("                       details.\n");
    mprint ("    --no_progress_bar: Suppress the output of the progress bar\n");
	mprint ("               -quiet: Don't write any message.\n");
    mprint ("\n");
	mprint ("Notes on the CEA-708 decoder: While it is starting to be useful, it's\n");
	mprint ("a work in progress. A number of things don't work yet in the decoder\n");
	mprint ("itself, and many of the auxiliary tools (case conversion to name one)\n");
	mprint ("won't do anything yet. Feel free to submit samples that cause problems\n");
	mprint ("and feature requests.\n");
	mprint ("\n");
}

void parse_708services (char *s)
{
	char *c, *e, *l;
	if (s==NULL)
		return;
	l=s+strlen (s);
	for (c=s; c<l && *c; )
	{
		int svc=-1;
		while (*c && !isdigit (*c))
			c++;
		if (!*c) // We're done
			break; 
		e=c;
		while (isdigit (*e))
			e++;
		*e=0;
		svc=atoi (c);
		if (svc<1 || svc>63)
			fatal (EXIT_MALFORMED_PARAMETER, "Invalid service number (%d), valid range is 1-63.");
		cea708services[svc-1]=1;
		do_cea708=1;
		c=e+1;
	}
}

void parse_parameters (int argc, char *argv[])
{
	char *cea708_service_list=NULL; // List CEA-708 services

    // Sensible default values for credits
    stringztoms (DEF_VAL_STARTCREDITSNOTBEFORE, &startcreditsnotbefore);
    stringztoms (DEF_VAL_STARTCREDITSNOTAFTER, &startcreditsnotafter); 
    stringztoms (DEF_VAL_STARTCREDITSFORATLEAST, &startcreditsforatleast);
    stringztoms (DEF_VAL_STARTCREDITSFORATMOST, &startcreditsforatmost);
    stringztoms (DEF_VAL_ENDCREDITSFORATLEAST, &endcreditsforatleast);
    stringztoms (DEF_VAL_ENDCREDITSFORATMOST, &endcreditsforatmost);

    // Parse parameters
    for (int i=1; i<argc; i++)
    {
		if (strcmp (argv[i], "-")==0)
		{
			input_is_stdin=1;
			live_stream=-1;
			continue;
		}
        if (argv[i][0]!='-')
        {            
            int rc;
            if (argv[i][strlen (argv[i])-1]!='+')
            {
                rc=append_file_to_queue (argv[i]);
            }
            else
            {
                argv[i][strlen (argv[i])-1]=0;
                rc=add_file_sequence (argv[i]);
            }
            if (rc)
            {
                fatal (EXIT_NOT_ENOUGH_MEMORY, "Fatal: Not enough memory.\n");                
            }
        }
        if (strcmp (argv[i],"-bi")==0 ||
            strcmp (argv[i],"--bufferinput")==0)
            buffer_input = 1;
        if (strcmp (argv[i],"-nobi")==0 ||
            strcmp (argv[i],"--nobufferinput")==0)
            buffer_input = 0;
        if (strcmp (argv[i],"-dru")==0)
            direct_rollup = 1;
        if (strcmp (argv[i],"-nofc")==0 ||
            strcmp (argv[i],"--nofontcolor")==0)
            nofontcolor=1;
        if (strcmp (argv[i],"-nots")==0 ||
            strcmp (argv[i],"--notypesetting")==0)
            nofontcolor=1;
            
        /* Input file formats */
        if (    strcmp (argv[i],"-es")==0 ||
                strcmp (argv[i],"-ts")==0 ||
                strcmp (argv[i],"-ps")==0 ||
                strcmp (argv[i],"-nots")==0 ||
                strcmp (argv[i],"-asf")==0 ||
				strcmp (argv[i],"-mp4")==0 ||
                strcmp (argv[i],"--dvr-ms")==0 )                
            set_input_format (argv[i]); 
        if (strncmp (argv[i],"-in=", 4)==0)
            set_input_format (argv[i]+4);
            
        /* Output file formats */        
        if (strcmp (argv[i],"-srt")==0 ||
           strcmp (argv[i],"-sami")==0 || strcmp (argv[i],"-smi")==0 ||
           strcmp (argv[i],"--transcript")==0 || strcmp (argv[i],"-txt")==0 ||
		   strcmp (argv[i],"--timedtranscript")==0 || strcmp (argv[i],"-ttxt")==0 ||
		   strcmp (argv[i],"-null")==0)
           set_output_format (argv[i]);
        if (strncmp (argv[i],"-out=", 5)==0)
            set_output_format (argv[i]+5);
        
        /* Credit stuff */
        if ((strcmp (argv[i],"--startcreditstext")==0)
            && i<argc-1)
        {
            start_credits_text=argv[i+1];
            i++;
        }
        if ((strcmp (argv[i],"--startcreditsnotbefore")==0)
            && i<argc-1)
        {
            if (stringztoms (argv[i+1],&startcreditsnotbefore)==-1)
            {
                fatal (EXIT_MALFORMED_PARAMETER, "--startcreditsnotbefore only accepts SS, MM:SS or HH:MM:SS\n");
            }
            i++;
        }
        if ((strcmp (argv[i],"--startcreditsnotafter")==0)
            && i<argc-1)
        {
            if (stringztoms (argv[i+1],&startcreditsnotafter)==-1)
            {
                fatal (EXIT_MALFORMED_PARAMETER, "--startcreditsnotafter only accepts SS, MM:SS or HH:MM:SS\n");
            }
            i++;
        }
        if ((strcmp (argv[i],"--startcreditsforatleast")==0)
            && i<argc-1)
        {
            if (stringztoms (argv[i+1],&startcreditsforatleast)==-1)
            {
                fatal (EXIT_MALFORMED_PARAMETER, "--startcreditsforatleast only accepts SS, MM:SS or HH:MM:SS\n");
            }
            i++;
        }
        if ((strcmp (argv[i],"--startcreditsforatmost")==0)
            && i<argc-1)
        {
            if (stringztoms (argv[i+1],&startcreditsforatmost)==-1)
            {
                fatal (EXIT_MALFORMED_PARAMETER, "--startcreditsforatmost only accepts SS, MM:SS or HH:MM:SS\n");
            }
            i++;
        }
        
        if  ((strcmp (argv[i],"--endcreditstext")==0 )
            && i<argc-1)
        {
            end_credits_text=argv[i+1];
            i++;
        }
        if ((strcmp (argv[i],"--endcreditsforatleast")==0)
            && i<argc-1)
        {
            if (stringztoms (argv[i+1],&endcreditsforatleast)==-1)
            {
                fatal (EXIT_MALFORMED_PARAMETER, "--endcreditsforatleast only accepts SS, MM:SS or HH:MM:SS\n");
            }
            i++;
        }
        if ((strcmp (argv[i],"--endcreditsforatmost")==0)
            && i<argc-1)
        {
            if (stringztoms (argv[i+1],&endcreditsforatmost)==-1)
            {
                fatal (EXIT_MALFORMED_PARAMETER, "--startcreditsforatmost only accepts SS, MM:SS or HH:MM:SS\n");
            }
            i++;
        }

        /* More stuff */
        if (strcmp (argv[i],"-ve")==0 ||
            strcmp (argv[i],"--videoedited")==0)
            binary_concat=0;
        if (strcmp (argv[i],"-12")==0)
            extract = 12;
        if (strcmp (argv[i],"-gt")==0 || 
            strcmp (argv[i],"--goptime")==0)
            use_gop_as_pts = 1;
        if (strcmp (argv[i],"-fp")==0 || 
            strcmp (argv[i],"--fixpadding")==0)
            fix_padding = 1;
        if (strcmp (argv[i],"-90090")==0)
            MPEG_CLOCK_FREQ=90090;
        if (strcmp (argv[i],"-noru")==0 || 
            strcmp (argv[i],"--norollup")==0)
            norollup = 1;
        if (strcmp (argv[i],"-trim")==0)
            trim_subs=1;
        if (strcmp (argv[i],"--gui_mode_reports")==0)
        {
            gui_mode_reports=1;
            no_progress_bar=1;
            // Do it as soon as possible, because it something fails we might not have a chance
            activity_report_version(); 
        }
        if (strcmp (argv[i],"--no_progress_bar")==0)
            no_progress_bar=1;
        if (strcmp (argv[i],"--sentencecap")==0 ||
            strcmp (argv[i],"-sc")==0)
        {
            if (add_built_in_words())
                fatal (EXIT_NOT_ENOUGH_MEMORY, "Not enough memory for word list");
            sentence_cap=1;
        }
        if ((strcmp (argv[i],"--capfile")==0 ||
            strcmp (argv[i],"-caf")==0)
            && i<argc-1)
        {
            if (add_built_in_words())
                fatal (EXIT_NOT_ENOUGH_MEMORY, "Not enough memory for word list");
            if (process_cap_file (argv[i+1])!=0)
                fatal (EXIT_ERROR_IN_CAPITALIZATION_FILE, "There was an error processing the capitalization file.\n");
            sentence_cap=1;
            sentence_cap_file=argv[i+1];
            i++;
        }
        if (strcmp (argv[i],"--program-number")==0 ||
            strcmp (argv[i],"-pn")==0)
        {
            if (i==argc-1 // Means no following argument 
                || !isanumber (argv[i+1])) // Means is not a number
                ts_forced_program = (unsigned)-1; // Autodetect
            else
            {
                ts_forced_program=atoi (argv[i+1]);
                ts_forced_program_selected=1;
                i++;
            }
        }

        if (strcmp (argv[i],"--stream")==0 ||
            strcmp (argv[i],"-s")==0)
        {
            if (i==argc-1 // Means no following argument 
                || !isanumber (argv[i+1])) // Means is not a number
                live_stream=-1; // Live stream without timeout
            else
            {
                live_stream=atoi (argv[i+1]);
                i++;
            }
        }
        if ((strcmp (argv[i],"--defaultcolor")==0 ||
            strcmp (argv[i],"-dc")==0)
            && i<argc-1)
        {
            if (strlen (argv[i+1])!=7 || argv[i+1][0]!='#')
            {
                fatal (EXIT_MALFORMED_PARAMETER, "--defaultcolor expects a 7 character parameter that starts with #\n");                
            }
            strcpy ((char *) usercolor_rgb,argv[i+1]);
            default_color=COL_USERDEFINED;
            i++;
        }
        if (strcmp (argv[i],"-delay")==0 && i<argc-1)
        {
            if (parsedelay (argv[i+1]))
            {
                fatal (EXIT_MALFORMED_PARAMETER, "-delay only accept integers (such as -300 or 300)\n");                
            }
            i++;
        }
        if ((strcmp (argv[i],"-scr")==0 || 
            strcmp (argv[i],"--screenfuls")==0) && i<argc-1)
        {
            screens_to_process=atoi (argv[i+1]);
            if (screens_to_process<0)
            {
                fatal (EXIT_MALFORMED_PARAMETER, "--screenfuls only accepts positive integers.\n");                
            }
            i++;
        }
        if (strcmp (argv[i],"-startat")==0 && i<argc-1)
        {
            if (stringztoms (argv[i+1],&extraction_start)==-1)
            {
                fatal (EXIT_MALFORMED_PARAMETER, "-startat only accepts SS, MM:SS or HH:MM:SS\n");
            }
            i++;
        }
        if (strcmp (argv[i],"-endat")==0 && i<argc-1)
        {
            if (stringztoms (argv[i+1],&extraction_end)==-1)
            {
                fatal (EXIT_MALFORMED_PARAMETER, "-endat only accepts SS, MM:SS or HH:MM:SS\n");                
            }
            i++;
        }		
        if (strcmp (argv[i],"-1")==0)
            extract = 1;
        if (strcmp (argv[i],"-2")==0)
            extract = 2;
        if (strcmp (argv[i],"-cc2")==0 || strcmp (argv[i],"-CC2")==0)
            cc_channel=2;
        if (strcmp (argv[i],"-stdout")==0)		
		{
			if (messages_target==1) // Only change this if still stdout. -quiet could set it to 0 for example
				messages_target=2; // stderr			
			cc_to_stdout=1;
		}
		if (strcmp (argv[i],"-quiet")==0)		
		{
			messages_target=0;
		}
        if (strcmp (argv[i],"-debug")==0)
			debug_mask |= DMT_VERBOSE;            
        if (strcmp (argv[i],"-608")==0)
			debug_mask |= DMT_608;
        if (strcmp (argv[i],"-708")==0)
            debug_mask |= DMT_708;    
        if (strcmp (argv[i],"-goppts")==0) 
            debug_mask |= DMT_TIME;
        if (strcmp (argv[i],"-vides")==0)
            debug_mask |= DMT_VIDES;
        if (strcmp (argv[i],"-xdsdebug")==0)
			debug_mask |= DMT_XDS;
        if (strcmp (argv[i],"-parsedebug")==0)
			debug_mask |= DMT_PARSE;
        if (strcmp (argv[i],"-cbraw")==0)
			debug_mask |= DMT_CBRAW;
        if (strcmp (argv[i],"-tverbose")==0)
		{
			debug_mask |= DMT_TELETEXT;            
			tlt_config.verbose=1;
		}		
        if (strcmp (argv[i],"-fullbin")==0)
            fullbin = 1;
        if (strcmp (argv[i],"-nosync")==0)
            nosync = 1;
        if (strcmp (argv[i],"-haup")==0 || strcmp (argv[i],"--hauppauge")==0)
			hauppauge_mode = 1;
        if (strcmp (argv[i],"-mp4vidtrack")==0)
			mp4vidtrack = 1;		
        if (strstr (argv[i],"-unicode")!=NULL)
            encoding=ENC_UNICODE;
        if (strstr (argv[i],"-utf8")!=NULL)
            encoding=ENC_UTF_8;
        if (strcmp (argv[i],"-poc")==0 || strcmp (argv[i],"--usepicorder")==0)
			usepicorder = 1;
        if (strstr (argv[i],"-myth")!=NULL)
            auto_myth=1;        
        if (strstr (argv[i],"-nomyth")!=NULL)
            auto_myth=0;
        if (strstr (argv[i],"-wtvconvertfix")!=NULL)
            wtvconvertfix=1;
        if (strcmp (argv[i],"-o")==0 && i<argc-1)
        {
            output_filename=argv[i+1];
            i++;
        }
        if (strcmp (argv[i],"-cf")==0 && i<argc-1)
        {
            out_elementarystream_filename=argv[i+1];
            i++;
        }
        if (strcmp (argv[i],"-o1")==0 && i<argc-1)
        {
            wbout1.filename=argv[i+1];
            i++;
        }
        if (strcmp (argv[i],"-o2")==0 && i<argc-1)
        {
            wbout2.filename=argv[i+1];
            i++;
        }
        if ( (strcmp (argv[i],"-svc")==0 || strcmp (argv[i],"--service")==0) &&
			i<argc-1)
		{
			cea708_service_list=argv[i+1];
			parse_708services (cea708_service_list);
			i++;
		}
		/* Teletext stuff */
        if (strcmp (argv[i],"-tpage")==0 && i<argc-1)
		{
			tlt_config.page = atoi(argv[i+1]);
			i++;
		}		
        // Unrecognized switches are silently ignored
    }	
}


/* 
Serval Distributed Numbering Architecture (DNA)
Copyright (C) 2010 Paul Gardner-Stephen 

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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "mphlr.h"
#include "rhizome.h"
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>

char *gatewayspec=NULL;

char *outputtemplate=NULL;
char *instrumentation_file=NULL;
char *importFile=NULL;

int debug=0;
int timeout=3000; /* 3000ms request timeout */

int serverMode=0;
int clientMode=0;

int returnMultiVars=0;

int hexdigit[16]={'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};

struct mphlr_variable vars[]={
  /* Variables that can have only a single value */
  {VAR_EOR,"eor","Marks end of record"},
  {VAR_CREATETIME,"createtime","Time HLR record was created"},
  {VAR_CREATOR,"creator","Device that created this HLR record"},
  {VAR_REVISION,"revision","Revision number of this HLR record"},
  {VAR_REVISOR,"revisor","Device that revised this HLR record"},
  {VAR_PIN,"pin","Secret PIN for this HLR record"},

  /* GSM encoded audio, so a 16KB MPHLR maximum size shouldn't
     pose a problem.  8KB = ~4.5 seconds, which is a long time 
     to say your name in, leaving 8KB for other variables. */
  {VAR_VOICESIG,"voicesig","Voice signature of this subscriber"},

  {VAR_HLRMASTER,"hlrmaster","Location where the master copy of this HLR record is maintained."},

  /* Variables that can take multiple values */
  {VAR_DIDS,"dids","Numbers claimed by this subscriber"},
  {VAR_LOCATIONS,"locations","Locations where this subscriber wishes to receive calls"},
  {VAR_IEMIS,"iemis","GSM IEMIs claimed by this subscriber"},
  {VAR_TEMIS,"temis","GSM TEMIs claimed by this subscriber"},

  /* Each entry here has a flag byte (unread, ...) */
  {VAR_CALLS_IN,"callsin","Calls received by this subscriber"},
  {VAR_CALLS_MISSED,"callsmissed","Calls missed by this subscriber"},
  {VAR_CALLS_OUT,"callsout","Calls made by this subscriber"},

  {VAR_SMESSAGES,"smessages","SMS received by this subscriber"},

  {VAR_DID2SUBSCRIBER,"did2subscriber","Preferred subscribers for commonly called DIDs"},

  {VAR_HLRBACKUPS,"hlrbackups","Locations where backups of this HLR record are maintained."},

  {VAR_NOTE,"note","Free-form notes on this HLR record"},

  {0x00,NULL,NULL}
};

int sock=-1;

#ifndef HAVE_BZERO
/* OpenWRT doesn't have bzero */
void bzero(void *m,size_t len)
{
  unsigned char *c=m;
  int i;
  for(i=0;i<len;i++) c[i]=0;
}
#endif

int dump(char *name,unsigned char *addr,int len)
{
  int i,j;
  fprintf(stderr,"Dump of %s\n",name);
  for(i=0;i<len;i+=16) 
    {
      fprintf(stderr,"  %04x :",i);
      for(j=0;j<16&&(i+j)<len;j++) fprintf(stderr," %02x",addr[i+j]);
      for(;j<16;j++) fprintf(stderr,"   ");
      fprintf(stderr,"    ");
      for(j=0;j<16&&(i+j)<len;j++) fprintf(stderr,"%c",addr[i+j]>=' '&&addr[i+j]<0x7f?addr[i+j]:'.');
      fprintf(stderr,"\n");
    }
  return 0;
}

int dumpResponses(struct response_set *responses)
{
  struct response *r;
  if (!responses) {fprintf(stderr,"Response set is NULL\n"); return 0; }
  fprintf(stderr,"Response set claims to contain %d entries.\n",responses->response_count);
  r=responses->responses;
  while(r)
    {
      fprintf(stderr,"  response code 0x%02x\n",r->code);
      if (r->next)
	if (r->next->prev!=r) fprintf(stderr,"    !! response chain is broken\n");
      r=r->next;
    }
  return 0;
}

int setReason(char *fmt, ...)
{
  va_list ap,ap2;
  char msg[8192];

  va_start(ap,fmt);
  va_copy(ap2,ap);

  vsnprintf(msg,8192,fmt,ap2); msg[8191]=0;

  va_end(ap);

  fprintf(stderr,"Error: %s\n",msg);
  return -1;
}


int hexvalue(unsigned char c)
{
  if (c>='0'&&c<='9') return c-'0';
  if (c>='A'&&c<='F') return c-'A'+10;
  if (c>='a'&&c<='f') return c-'a'+10;
  return setReason("Invalid hex digit in SID");
}

int parseAssignment(unsigned char *text,int *var_id,unsigned char *value,int *value_len)
{
  /* Parse an assignment.

     Valid formats are:

     var=@file   - value comes from named file.
     var=[[$]value] - value comes from string, and may be empty.  $ means value is in hex

     Values are length limited to 65535 bytes.
  */

  int i,v;
  int max_len=*value_len;
  int vlen=0;
  int tlen=strlen((char *)text);

  if (tlen>3072) {
    return setReason("Variable assignment string is too long, use =@file to read value from a file");
  }

  /* Identify which variable */
  for(i=0;i<tlen;i++) if (text[i]=='=') break;
  for(v=0;vars[v].name;v++)   if (!strncasecmp(vars[v].name,(char *)text,i)) break;

  if (!vars[v].name) return setReason("Illegal variable name in assignment");
  *var_id=vars[v].id;

  i++;
  switch(text[i])
    {
    case '$': /* hex */
      i++;
      while(i<tlen) {
	int b=hexvalue(text[i++])<<4;
	if (i>=tlen) return setReason("Variable value has an odd number of hex digits.");
	b|=hexvalue(text[i++]);
	if (b<0) return setReason("That doesn't look like hex to me");
	if (vlen>=max_len) return setReason("Variable hex value too long");
	value[vlen++]=b;
      }
      *value_len=vlen;
      return 0;
      break;
    case '@': /* file */
      {
	FILE *f=fopen((char *)&text[i+1],"r");
	int flen;
	fseek(f,0,SEEK_END);
	flen=ftell(f);
	if (flen>max_len) return setReason("Variable value from file too long");
	fseek(f,0,SEEK_SET);
	vlen=fread(value,1,flen,f);
	if (vlen!=flen) return setReason("Could not read all of file");
	fclose(f);
	*value_len=vlen;
	return 0;
      }
      break;
    default: /* literal string */
      vlen=strlen((char *)&text[i]);
      if (vlen>max_len) return setReason("Variable value too long");
      bcopy(&text[i],value,vlen);
      *value_len=vlen;
      return 0;
    }

  return 0;
}

int usage(char *complaint)
{
  fprintf(stderr,"dna: %s\n",complaint);
  fprintf(stderr,"usage:\n");
  fprintf(stderr,"   dna [-v <flags>] -S <hlr size in MB> [-f HLR backing file] [-I import.txt] [-N interface,...] [-G gateway specification] [-r rhizome path]\n");
  fprintf(stderr,"or\n");
  fprintf(stderr,"   dna [-v <flags>] -f <HLR backing file> -E <hlr export file>\n");
  fprintf(stderr,"or\n");
  fprintf(stderr,"   dna -r <rhizome path> -M <manifest name>\n");
  fprintf(stderr,"or\n");
  fprintf(stderr,"   dna <-d|-s> id -A\n");
  fprintf(stderr,"or\n");
  fprintf(stderr,"   dna <-d|-s> id [-p pin] [-i variable instance] <-R variable[=value]>\n");
  fprintf(stderr,"       [-v <flags>] [-t request timeout in ms] [-O output file name template]\n");
  fprintf(stderr,"or\n");
  fprintf(stderr,"   dna <-d|-s> id [-p pin] [-i variable instance] <-W|-U|-D variable[=[$|@]value]>\n");
  fprintf(stderr,"       [-v <flags>] [-t request timeout in ms]\n");
  fprintf(stderr,"or\n");
  fprintf(stderr,"   dna [-v <flags>] [-t timeout] -d did -C\n");
  fprintf(stderr,"or\n");
  fprintf(stderr,"   dna [-v <flags>] -f <hlr.dat> -E <export.txt>\n");

  fprintf(stderr,"\n");
  fprintf(stderr,"       -v - Set verbosity.\n");
  fprintf(stderr,"       -E - Export specified HLR database into specified flat text file.\n");
  fprintf(stderr,"       -I - Import a previously exported HLR database into this one.\n");
  fprintf(stderr,"       -A - Ask for address of subscriber.\n");
  fprintf(stderr,"       -b - Specify BATMAN socket to obtain peer list (flaky).\n");
  fprintf(stderr,"       -l - Specify BATMAN socket to obtain peer list (better, but requires Serval patched BATMAN).\n");
  fprintf(stderr,"       -L - Log mesh statistics to specified file.\n");
  fprintf(stderr,"       -m - Return multiple variable values instead of only first response.\n");
  fprintf(stderr,"       -M - Create and import a new bundle from the specified manifest.\n");
  fprintf(stderr,"       -n - Do not detach from foreground in server mode.\n");
  fprintf(stderr,"       -S - Run in server mode with an HLR of the specified size.\n");
  fprintf(stderr,"       -f - Use the specified file as a permanent store for HLR data.\n");
  fprintf(stderr,"       -d - Search by Direct Inward Dial (DID) number.\n");
  fprintf(stderr,"       -s - Search by Subscriber ID (SID) number.\n");
  fprintf(stderr,"       -p - Specify additional DNA nodes to query.\n");
  fprintf(stderr,"       -P - Authenticate using the supplied pin.\n");
  fprintf(stderr,"       -r - Enable Rhizome store-and-forward transport using the specified data store directory.\n");
  fprintf(stderr,"            To limit the storage: echo space=[KB] > path/rhizome.conf\n");
  fprintf(stderr,"       -R - Read a variable value.\n");
  fprintf(stderr,"       -O - Place read variable value into files using argument as a template.\n");
  fprintf(stderr,"            The following template codes can be used (interpretted by sprintf):\n");
  fprintf(stderr,"               %%1$s - Subscriber ID\n");
  fprintf(stderr,"               %%2$d - Variable ID (0-255)\n");
  fprintf(stderr,"               %%3$d - Variable instance number (0-255)\n");
  fprintf(stderr,"       -W - Write a variable value, keeping previous values.\n");
  fprintf(stderr,"       -U - Update a variable value, replacing the previous value.\n");
  fprintf(stderr,"       -D - Delete a variable value.\n");
  fprintf(stderr,"            $value means interpret value as hexidecimal bytes.\n");
  fprintf(stderr,"            @value means read value from file called value.\n");
  fprintf(stderr,"       -C - Request the creation of a new subscriber with the specified DID.\n");
  fprintf(stderr,"       -t - Specify the request timeout period.\n");
  fprintf(stderr,"       -G - Offer gateway services.  Argument specifies locations of necessary files.\n");
  fprintf(stderr,"            Use -G [potato|android|custom:...] to set defaults for your device type.\n");
  fprintf(stderr,"       -N - Specify one or more interfaces for the DNA overlay mesh to operate.\n");
  fprintf(stderr,"            Interface specifications take the form <+|->[interface[=type][,...]\n");
  fprintf(stderr,"            e.g., -N -en0,+ to use all interfaces except en0\n");
  fprintf(stderr,"\n");
  exit(-1);
}

#ifndef DNA_NO_MAIN
char *exec_args[128];
int exec_argc=0;
void signal_handler( int signal ) {
  /* oops - caught a bad signal -- exec() ourselves fresh */
  if (sock>-1) close(sock);
  execv(exec_args[0],exec_args);
  /* Quit if the exec() fails */
  exit(-3);
} 

int main(int argc,char **argv)
{
  int c;
  char *pin=NULL;
  char *did=NULL;
  char *sid=NULL;
  char *hlr_file=NULL;
  int instance=-1;
  int foregroundMode=0;

#if defined WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(1,1), &wsa_data);
#else
    /* Catch sigsegv and other crash signals so that we can relaunch ourselves */

    for(exec_argc=0;exec_argc<argc;exec_argc++)
      exec_args[exec_argc]=strdup(argv[exec_argc]);
    exec_args[exec_argc]=0;

    signal( SIGSEGV, signal_handler );
    signal( SIGFPE, signal_handler );
    signal( SIGILL, signal_handler );
    signal( SIGBUS, signal_handler );
    signal( SIGABRT, signal_handler );    
#endif

  srandomdev();

  while((c=getopt(argc,argv,"Ab:B:E:G:I:S:f:d:i:l:L:mnp:P:r:s:t:v:R:W:U:D:CO:M:N:")) != -1 ) 
    {
      switch(c)
	{
	case 'r': /* Enable rhizome */
	  if (rhizome_datastore_path) return WHY("-r specified more than once");
	  rhizome_datastore_path=optarg;
	  rhizome_opendb();
	  /* Also set hlr file to be in the Rhizome directory, to save the need to specify it
	     separately. */
	  char temp[1024];temp[1023]=0;
	  snprintf(temp,1024,"%s/hlr.dat",optarg); 
	  if (temp[1023]) {
	    exit(WHY("Rhizome directory name too long."));
	  }
	  hlr_file=strdup(temp);

	  break;
	case 'M': /* Distribute specified manifest and file pair using Rhizome. */
	  /* This option assumes that the manifest is locally produced, and will
	     create any appropriate signatures, replacing any old signatures on the
	     manifest.
	     A different calling would be required to import an existing pre-signed
	     manifest */
	  return rhizome_bundle_import(optarg,
				       NULL /* no groups - XXX should allow them */,
				       255 /* ttl - XXX should read from somewhere,
					      e.g., bar if being imported */,
				       0 /* int verifyP */, 
				       1 /* int checkFileP */, 
				       1 /* int signP */);
	  break;
	case 'm': returnMultiVars=1; break;
	case 'N': /* Ask for overlay network to setup one or more interfaces */
	  if (overlay_interface_args(optarg))
	    return WHY("Invalid interface specification(s) passed to -N");
	  overlayMode=1;
	  break;
	case 'G': /* Offer gateway services */
          gatewayspec=strdup(optarg);
	  if(prepareGateway(gatewayspec)) return usage("Invalid gateway specification");
          break;
        case 'E': /* Export HLR into plain text file that can be imported later */
          if (!hlr_file) usage("You must specify an HLR file to export from, i.e., dna -f hlr.dat -E hlr.txt");
          return exportHlr((unsigned char*)hlr_file,optarg);
          break;
        case 'I': /* Import HLR data from a plain text file into current HLR */
          if (importFile) usage("-I multiply specified.");
          importFile=optarg;
          if (!hlr_file||!serverMode) usage("-I requires -S and -f.");
          if (openHlrFile(hlr_file,hlr_size))
            exit(setReason("Failed to open HLR database"));
          importHlr(importFile);
          return 0;
          break;
	case 'n': /* don't detach from foreground in server mode */
	  foregroundMode=1; break;
	case 'b': /* talk peers on a BATMAN mesh */
	  batman_socket=strdup(optarg);
	  break;
	case 'l': /* talk peers on a BATMAN mesh */
	  batman_peerfile=strdup(optarg);
	  break;
	case 'L':
	  instrumentation_file=strdup(optarg);
	  break;
	case 'B': /* Set simulated Bit Error Rate for bench-testing */
	  simulatedBER=atof(optarg);
	  fprintf(stderr,"WARNING: Bit error injection enabled -- this will cause packet loss and is intended only for testing.\n");
	  break;
	case 'S': 
	  if (atof(optarg)<0.1||atof(optarg)>16384) usage("HLR must be 0.1MB - 16384MB in size.");
	  hlr_size=(int)(atof(optarg)*1048576.0);
	  serverMode=1; 
	  break;
	case 'i':
	  instance=atoi(optarg);
	  if (instance<-1||instance>255) usage("Illegal variable instance ID.");
	  break;
	case 'f':
	  if (clientMode) usage("Only servers use backing files");
	  hlr_file=strdup(optarg);
	  break;
	case 'p': /* additional peers to query */
	  if (additionalPeer(optarg)) exit(-3);
	  break;
	case 'P': /* Supply pin */
	  pin=strdup(optarg);
	  clientMode=1;
	  break;
	case 'd': /* Ask by DID */
	  clientMode=1;
	  did=strdup(optarg);
	  break;
	case 's': /* Ask by subscriber ID */
	  clientMode=1;
	  sid=strdup(optarg);
	  break;
	case 't': /* request timeout (ms) */
	  timeout=atoi(optarg);
	  break;
	case 'v': /* set verbosity */
	  debug=strtoll(optarg,NULL,10);
	  if (strstr(optarg,"interfaces")) debug|=DEBUG_OVERLAYINTERFACES;
	  if (strstr(optarg,"packetxfer")) debug|=DEBUG_PACKETXFER;
	  if (strstr(optarg,"verbose")) debug|=DEBUG_VERBOSE;
	  if (strstr(optarg,"verbio")) debug|=DEBUG_VERBOSE_IO;
	  if (strstr(optarg,"peers")) debug|=DEBUG_PEERS;
	  if (strstr(optarg,"dnaresponses")) debug|=DEBUG_DNARESPONSES;
	  if (strstr(optarg,"dnarequests")) debug|=DEBUG_DNAREQUESTS;
	  if (strstr(optarg,"simulation")) debug|=DEBUG_SIMULATION;
	  if (strstr(optarg,"dnavars")) debug|=DEBUG_DNAVARS;
	  if (strstr(optarg,"packetformats")) debug|=DEBUG_PACKETFORMATS;
	  if (strstr(optarg,"gateway")) debug|=DEBUG_GATEWAY;
	  if (strstr(optarg,"hlr")) debug|=DEBUG_HLR;
	  if (strstr(optarg,"sockio")) debug|=DEBUG_IO;
	  if (strstr(optarg,"frames")) debug|=DEBUG_OVERLAYFRAMES;
	  if (strstr(optarg,"abbreviations")) debug|=DEBUG_OVERLAYABBREVIATIONS;
	  if (strstr(optarg,"routing")) debug|=DEBUG_OVERLAYROUTING;
	  if (strstr(optarg,"security")) debug|=DEBUG_SECURITY;
	  if (strstr(optarg,"rhizome")) debug|=DEBUG_RHIZOME;
	  if (strstr(optarg,"monitorroutes")) debug|=DEBUG_OVERLAYROUTEMONITOR;
	  if (strstr(optarg,"queues")) debug|=DEBUG_QUEUES;
	  if (strstr(optarg,"broadcasts")) debug|=DEBUG_BROADCASTS;
	  break;
	case 'A': /* get address (IP or otherwise) of a given peer */
	  peerAddress(did,sid,3 /* 1 = print list of addresses to stdout, 2 = set peer list to responders */);
	  break;
	case 'R': /* read a variable */
	  {	    
	    unsigned char buffer[65535];
	    int len=0;
	    requestItem(did,sid,(char *)optarg,instance,buffer,sizeof(buffer),&len,NULL);
	  }
	  break;
	case 'W': /* write a variable */
	  {	    
	    int var_id;
	    unsigned char value[65536];
	    int value_len=65535;
	    if (parseAssignment((unsigned char *)optarg,&var_id,value,&value_len)) return -1;
	    value[value_len]=0;
	    return writeItem(did?did:sid,var_id,instance,value,0,value_len,SET_NOREPLACE,NULL);
	  }
	  break;
	case 'U': /* write or update a variable */
	  {	    
	    int var_id;
	    unsigned char value[65536];
	    int value_len=65535;
	    if (parseAssignment((unsigned char *)optarg,&var_id,value,&value_len)) return -1;
	    value[value_len]=0;
	    return writeItem(did?did:sid,var_id,instance,value,0,value_len,SET_REPLACE,NULL);
	  }
	  break;
	case 'C': /* create a new HLR entry */
	  {
	    if (optind<argc) usage("Extraneous options after HLR creation request");
	    if ((!did)||(sid)) usage("Specify exactly one DID and no SID to create a new HLR entry");
	    return requestNewHLR(did,pin,sid,NULL);
	  }
	  break;
	case 'O': /* output to templated files */
	  if (outputtemplate) usage("You can only specify -O once");
	  outputtemplate=strdup(optarg);
	  break;
	default:
	  usage("Invalid option");
	  break;
	}
    }

  if (optind<argc) usage("Extraneous options at end of command");

  if (hlr_file&&clientMode) usage("Only servers use backing files");
  if (serverMode&&clientMode) usage("You asked me to be both server and client.  That's silly.");
  if (serverMode) return server(hlr_file,hlr_size,foregroundMode);
  if (!clientMode) usage("Mesh Potato Home Location Register (HLR) Tool.");

#if defined WIN32
    WSACleanup();
#endif

  /* Client mode: */
  return 0;
}
#endif

long long parse_quantity(char *q)
{
  int m;
  char units[80];

  if (strlen(q)>=80) return WHY("quantity string >=80 characters");

  if (sscanf(q,"%d%s",&m,units)==2)
    {
      if (units[1]) return WHY("Units should be single character");
      switch(units[0])
	{
	case 'k': return m*1000LL;
	case 'K': return m*1024LL;
	case 'm': return m*1000LL*1000LL;
	case 'M': return m*1024LL*1024LL;
	case 'g': return m*1000LL*1000LL*1000LL;
	case 'G': return m*1024LL*1024LL*1024LL;
	default:
	  return WHY("Illegal unit: should be k,K,m,M,g, or G.");
	}
    }
  if (sscanf(q,"%d",&m)==1)
    {
      return m;
    }
  else
    {
      return WHY("Could not parse quantity");
    }
}

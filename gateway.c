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
#include <sys/stat.h>
#include <sys/wait.h>

char *asterisk_extensions_conf="/data/data/org.servalproject/etc/asterisk/gatewayextensions.conf";
char *asterisk_binary="/data/data/org.servalproject/sbin/asterisk";
char *temp_file="/data/data/org.servalproject/var/temp.out";
char *cmd_file="/data/data/org.servalproject/var/temp.cmd";
char *shell="/system/bin/sh";

typedef struct dna_gateway_extension {
  char did[64];
  char requestor_sid[SID_SIZE+1];
  char uri[256-64-SID_SIZE-1-sizeof(int)-(sizeof(time_t)*2)];
  int uriprefixlen;
  time_t created; /* 0 == free */
  time_t expires; /* 0 == free */
} dna_gateway_extension;

#define MAX_CURRENT_EXTENSIONS 1024
dna_gateway_extension extensions[MAX_CURRENT_EXTENSIONS];

int prepareGateway(char *spec)
{
  if ((!strcasecmp(spec,"potato"))||(!strcasecmp(spec,"meshpotato"))||(!strcasecmp(spec,"mp"))||(!strcasecmp(spec,"mp1"))) {
    /* Setup for mesh potato */
    asterisk_extensions_conf="/etc/asterisk/gatewayextensions.conf";
    asterisk_binary="/usr/sbin/asterisk";
    temp_file="/var/dnatemp.out";
    cmd_file="/var/dnatemp.cmd";
    shell="/bin/sh";
    return 0;
  } else if ((!strcasecmp(spec,"android"))||(!strcasecmp(spec,"batphone"))) {
    /* Setup for android -- this is default, so don't change anything */
    return 0;
  } else if (!strncasecmp(spec,"custom:",7)) {
    char a[1024],b[1024],c[1024],d[1024],e[1024];
    if (sscanf(spec,"custom:%[^:]:%[^:]:%[^:]:%[^:]:%[^:]",a,b,c,d,e)!=5) return WHY("Invalid custom gateway specification");
    asterisk_extensions_conf=strdup(a);
    asterisk_binary=strdup(b);
    temp_file=strdup(c);
    cmd_file=strdup(d);
    shell=strdup(e);
    return 0;
  }
  else 
    return WHY("Invalid gateway specification");
}

int safeSystem(char *cmd_file)
{
  {
    int pid = fork();

    if(pid == -1) {
      fprintf(stderr, "Unable to fork.\n");
      return -1;
    } else if(pid == 0) {
      execlp(shell, shell, "-c", cmd_file,NULL);
      exit(1);
    } else {
      // Wait for child to finish
      int status;
      wait(&status);
      return WEXITSTATUS(status);
    }
  }
}

int runCommand(char *cmd)
{
  FILE *f=fopen(cmd_file,"w");
  if (!f) {
    if (debug&DEBUG_GATEWAY) fprintf(stderr,"%s:%d: Could not write to command file '%s'.\n",__FUNCTION__,__LINE__,cmd_file);    
    return 0;
  }
  fprintf(f,"#!%s\n%s\n",shell,cmd);
  fclose(f);
  if (chmod(cmd_file,0000700))
    {
    if (debug&DEBUG_GATEWAY) fprintf(stderr,"%s:%d: Could not chmod command file '%s'.\n",__FUNCTION__,__LINE__,cmd_file);    
    return 0;
    }
  return safeSystem(cmd_file);
}

int gatewayReadSettings(char *file)
{
  char line[1024];
  FILE *f=fopen(file,"r");
  if (!f) return -1;

  /* Location of extensions.conf file to write
     (really it would be a file you #include from extensions.conf) */
  line[0]=0; fgets(line,1024,f);
  asterisk_extensions_conf=strdup(line);

  /* The command required to get Asterisk to re-read the above file */
  line[0]=0; fgets(line,1024,f);
  asterisk_binary=strdup(line);

  /* Temporary file for catching Asterisk output from -rx commands */
  line[0]=0; fgets(line,1024,f);
  temp_file=strdup(line);

  /* Temporary file for running asterisk commands (since system() cannot do redirection on android) */
  line[0]=0; fgets(line,1024,f);
  cmd_file=strdup(line);

  line[0]=0; fgets(line,1024,f);
  shell=strdup(line);

  /* XXX Need more here I suspect */
  
  fclose(f);
  return 0;
}

int asteriskCreateExtension(char *requestor_sid,char *did,char *uri_out)
{
  /* XXX There are denial of service attacks that we have to consider here.
     Primarily, naughty persons could flood the gateway with false solicitations filling the
     current extension table and slowing asterisk down with lots of reloads.

     Our mitigation strategy is to use a random replacement scheme, except where the same SID
     has an entry in the table already, in which case we should replace that one.
     Replacement entails a search, which should be done using a tree structure for speed.
     Since right now we just want to get the functionality working, we will just do random replacement
     no matter what. 
  */

  /* XXX We should add authentication checks and number validity checks here, e.g., if a gateway only wants to
     allow access for a certain group of users, and/or to only a certain range of numbers */

  /* XXX The "secret" extension is only secret if we encrypt the reply packet! */

  int index=random()%MAX_CURRENT_EXTENSIONS;

  bcopy(requestor_sid,extensions[index].requestor_sid,SID_SIZE);
  strcpy(extensions[index].did,did);
  extensions[index].created=time(0);
  extensions[index].expires=time(0)+3600;
  snprintf(extensions[index].uri,sizeof(extensions[index].uri),"4101*%08x%04x@",
	   (unsigned int)random(),(unsigned int)random()&0xffff);
  extensions[index].uriprefixlen=strlen(extensions[index].uri)-1;
  if (extensions[index].uriprefixlen<0) {
    /* Whoops - something wrong with the extension/uri, so kill the record and fail. */
    extensions[index].expires=1;
    if (debug&DEBUG_GATEWAY) fprintf(stderr,"%s:%d: Generated extension appears to be malformed.\n",__FUNCTION__,__LINE__);
    return -1;
  }

  if (debug&DEBUG_GATEWAY) fprintf(stderr,"Created extension '%s' to dial %s\n",extensions[index].uri,did);
  strcpy(uri_out,extensions[index].uri);

  return 0;
}

int asteriskWriteExtensions()
{
  int i;
  time_t now=time(0);
  FILE *out;

  out=fopen(asterisk_extensions_conf,"w");
  if (!out) {
    if (debug&DEBUG_GATEWAY) fprintf(stderr,"%s:%d: Could not write extensions file '%s'.\n",__FUNCTION__,__LINE__,asterisk_extensions_conf);
    return -1;
  }

  for(i=0;i<MAX_CURRENT_EXTENSIONS;i++)
    {
      if (extensions[i].expires)
	{
	  if (extensions[i].expires<now)
	    {
	      /* Clear expired gateway extensions */
	      bzero(&extensions[i],sizeof(dna_gateway_extension));
	    }
	  else
	    {
	      extensions[i].uri[extensions[i].uriprefixlen]=0;
	      fprintf(out,
		      "exten => %s, 1, Dial(SIP/dnagateway/%s)\n"
		      "exten => %s, 2, Hangup()\n",
		      extensions[i].uri,
		      extensions[i].did,
		      extensions[i].uri);
	      extensions[i].uri[extensions[i].uriprefixlen]='@';
	    }
	}
    }
  fclose(out);
  return 0;
}

int asteriskReloadExtensions()
{
  char cmd[8192];
  snprintf(cmd,8192,"%s -rx 'dialplan reload'",asterisk_binary);
  if (runCommand(cmd))
    {
      if (debug&DEBUG_GATEWAY) fprintf(stderr,"%s:%d: Dialplan reload failed.\n",__FUNCTION__,__LINE__);
      return -1;
    }
  if (debug&DEBUG_GATEWAY) fprintf(stderr,"%s:%d: Dialplan reload appeared to succeed.\n",__FUNCTION__,__LINE__);  
  return 0;
}

int asteriskGatewayUpP()
{
  int registered=0;

  /* 
     1. Run "serval dna gateway" command to enquire of gateway status?
        No, as that enquires of the wrong DNA instance.  Also, we are now controlling the
	enable/disable by checking the outbound SIP gateway status in asterisk, and the
	BatPhone settings screen controls the availability of that by re-writing asterisk config files.
     2. Check that outbound SIP gateway is up: 
        asterisk -r "sip show registry"
	and grep output for active links.
	XXX - Annoyingly we need to know the server hostname to use the output of this list in
	a fool-proof manner.  However, if we work on the assumption of only one SIP registration existing, 
	being ours, then we can ignore the hostname.
   */
  char cmd[8192];

  unlink(temp_file);
  snprintf(cmd,8192,"%s -rx 'sip show registry' > %s",asterisk_binary,temp_file);

  if (runCommand(cmd)) {
    if (debug&DEBUG_GATEWAY) { fprintf(stderr,"%s:%d: system(%s) might have failed.\n",__FUNCTION__,__LINE__,cmd_file);
      perror("system"); }
  }
  FILE *f=fopen(temp_file,"r");
  if (!f) {
    if (debug&DEBUG_GATEWAY) fprintf(stderr,"%s:%d: Could not read result of \"sip show registry\" from '%s'.\n",__FUNCTION__,__LINE__,temp_file);
    return 0;
  }
  
  /* Output of command is something like:
     Host                            Username       Refresh State                Reg.Time                 
     houstin.voip.ms:5060            103585             120 Unregistered                                  
  */
  
  cmd[0]=0; fgets(cmd,1024,f);
  while(cmd[0]) {
    char host[1024];
    int port;
    char user[1024];
    int refresh;
    char state[1024];

    if (sscanf(cmd,"%[^:]:%d%*[ ]%[^ ]%*[ ]%d%*[ ]%[^ ]",
	       host,&port,user,&refresh,state)==5)
      {
	// state == "Unregistered" if unavailable, although other values are possible.
	// state == "Registered" if available.
	if (!strcasecmp(state,"Registered")) registered=1; else registered=0;
      }
    cmd[0]=0; fgets(cmd,1024,f);
  }
  
  fclose(f);

  return registered;
}

int asteriskObtainGateway(char *requestor_sid,char *did,char *uri_out)
{
    /* We use asterisk to provide the gateway service,
       so we need to create a temporary extension in extensions.conf,
       ask asterisk to re-read extensions.conf, and then make sure it has
       a functional SIP gateway.
    */
  
  if (!asteriskGatewayUpP()) 
    { if (debug&DEBUG_GATEWAY) fprintf(stderr,"Asterisk gatway is not up, so not offering gateway.\n"); return -1; }
  if (asteriskCreateExtension(requestor_sid,did,uri_out)) 
    {
      if (debug&DEBUG_GATEWAY) fprintf(stderr,"asteriskCreateExtension() failed, so not offering gateway.\n");
      return -1;
    }
  if (asteriskWriteExtensions())
    {
      if (debug&DEBUG_GATEWAY) fprintf(stderr,"asteriskWriteExtensions() failed, so not offering gateway.\n");
      return -1;
    }
  if (asteriskReloadExtensions()) 
    {
      if (debug&DEBUG_GATEWAY) fprintf(stderr,"asteriskReloadExtensions() failed, so not offering gateway.\n");
      return -1;
    }
  if (debug&DEBUG_GATEWAY) fprintf(stderr,"asteriskReloadExtensions() suceeded, offering gateway.\n");
  return 0;
}

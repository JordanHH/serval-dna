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

int overlay_payload_verify(overlay_frame *p)
{
  /* Make sure that an incoming payload has a valid signature from the sender.
     This is used to prevent spoofing */

  return WHY("function not implemented");
}

int op_append_type(overlay_buffer *headers,overlay_frame *p)
{
  unsigned char c[3];
  switch(p->type&OF_TYPE_FLAG_BITS)
    {
    case OF_TYPE_FLAG_NORMAL:
      c[0]=p->type|p->modifiers;
      if (debug&DEBUG_PACKETFORMATS) fprintf(stderr,"type resolves to %02x\n",c[0]);
      if (ob_append_bytes(headers,c,1)) return -1;
      break;
    case OF_TYPE_FLAG_E12:
      c[0]=(p->type&OF_MODIFIER_BITS)|OF_TYPE_EXTENDED12;
      c[1]=(p->type>>4)&0xff;
      if (debug&DEBUG_PACKETFORMATS) fprintf(stderr,"type resolves to %02x%02x\n",c[0],c[1]);
      if (ob_append_bytes(headers,c,2)) return -1;
      break;
    case OF_TYPE_FLAG_E20:
      c[0]=(p->type&OF_MODIFIER_BITS)|OF_TYPE_EXTENDED20;
      c[1]=(p->type>>4)&0xff;
      c[2]=(p->type>>12)&0xff;
      if (debug&DEBUG_PACKETFORMATS) fprintf(stderr,"type resolves to %02x%02x%02x\n",c[0],c[1],c[2]);
      if (ob_append_bytes(headers,c,3)) return -1;
      break;
    default: 
      /* Don't know this type of frame */
      WHY("Asked for format frame with unknown TYPE_FLAG bits");
      return -1;
    }
  return 0;
}


int overlay_frame_package_fmt1(overlay_frame *p,overlay_buffer *b)
{
  /* Convert a payload (frame) structure into a series of bytes.
     Assumes that any encryption etc has already been done.
     Will pick a next hop if one has not been chosen.
  */

  int nexthoplen=0;

  overlay_buffer *headers=ob_new(256);

  if (!headers) return WHY("could not allocate overlay buffer for headers");
  if (!p) return WHY("p is NULL");
  if (!b) return WHY("b is NULL");

  /* Build header */
  int fail=0;

  if (p->nexthop_address_status!=OA_RESOLVED) {
    if (overlay_get_nexthop((unsigned char *)p->destination,p->nexthop,&nexthoplen,&p->nexthop_interface)) fail++;
    else p->nexthop_address_status=OA_RESOLVED;
  }

  if (p->source[0]<0x10) {
    // Make sure that addresses do not overload the special address spaces of 0x00*-0x0f*
    fail++;
    return WHY("packet source address begins with reserved value 0x00-0x0f");
  }
  if (p->destination[0]<0x10) {
    // Make sure that addresses do not overload the special address spaces of 0x00*-0x0f*
    fail++;
    return WHY("packet destination address begins with reserved value 0x00-0x0f");
  }
  if (p->nexthop[0]<0x10) {
    // Make sure that addresses do not overload the special address spaces of 0x00*-0x0f*
    fail++;
    return WHY("packet nexthop address begins with reserved value 0x00-0x0f");
  }

  /* XXX Write fields in correct order */

  /* Write out type field byte(s) */
  if (!fail) if (op_append_type(headers,p)) fail++;

  /* Write out TTL */
  if (!fail) if (ob_append_byte(headers,p->ttl)) fail++;

  /* Length.  This is the fun part, because we cannot calculate how many bytes we need until
     we have abbreviated the addresses, and the length encoding we use varies according to the
     length encoded.  The simple option of running the abbreviations twice won't work because 
     we rely on context for abbreviating the addresses.  So we write it initially and then patch it
     after.
  */
  if (!fail) {
    int max_len=((SID_SIZE+3)*3+headers->length+p->payload->length);
    ob_append_rfs(headers,max_len);
    
    int addrs_start=headers->length;
    
    /* Write out addresses as abbreviated as possible */
    overlay_abbreviate_append_address(headers,p->nexthop);
    overlay_abbreviate_set_most_recent_address(p->nexthop);
    overlay_abbreviate_append_address(headers,p->destination);
    overlay_abbreviate_set_most_recent_address(p->destination);
    overlay_abbreviate_append_address(headers,p->source);
    overlay_abbreviate_set_most_recent_address(p->source);
    
    int addrs_len=headers->length-addrs_start;
    int actual_len=addrs_len+p->payload->length;
    ob_patch_rfs(headers,actual_len);
  }

  if (fail) {
    ob_free(headers);
    return WHY("failure count was non-zero");
  }

  /* Write payload format plus total length of header bits */
  if (ob_makespace(b,2+headers->length+p->payload->length)) {
    /* Not enough space free in output buffer */
    ob_free(headers);
    if (debug&DEBUG_PACKETFORMATS)
      WHY("Could not make enough space free in output buffer");
    return -1;
  }
  
  /* Package up headers and payload */
  ob_checkpoint(b);
  if (ob_append_bytes(b,headers->bytes,headers->length)) 
    { fail++; WHY("could not append header"); }
  if (ob_append_bytes(b,p->payload->bytes,p->payload->length)) 
    { fail++; WHY("could not append payload"); }

  /* XXX SIGN &/or ENCRYPT */
  
  ob_free(headers);
  
  if (fail) { ob_rewind(b); return WHY("failure count was non-zero"); } else return 0;
}
  
overlay_buffer *overlay_payload_unpackage(overlay_frame *b) {
  /* Extract the payload at the current location in the buffer. */
    
  WHY("not implemented");
  return NULL;
}

int overlay_payload_enqueue(int q,overlay_frame *p)
{
  /* Add payload p to queue q.

     Queues get scanned from first to last, so we should append new entries
     on the end of the queue.

     Complain if there are too many frames in the queue.
  */

  if (q<0||q>=OQ_MAX) return WHY("Invalid queue specified");
  if (!p) return WHY("Cannot queue NULL");

  if (overlay_tx[q].length>=overlay_tx[q].maxLength) return WHY("Queue congested");
  
  overlay_frame *l=overlay_tx[q].last;
  if (l) l->next=p;
  p->prev=l;
  p->next=NULL;
  p->enqueued_at=overlay_gettime_ms();

  overlay_tx[q].last=p;
  if (!overlay_tx[q].first) overlay_tx[q].first=p;
  overlay_tx[q].length++;
  
  return 0;
}

int op_free(overlay_frame *p)
{
  if (!p) return WHY("Asked to free NULL");
  if (p->prev&&p->prev->next==p) return WHY("p->prev->next still points here");
  if (p->next&&p->next->prev==p) return WHY("p->next->prev still points here");
  p->prev=NULL;
  p->next=NULL;
  if (p->payload) ob_free(p->payload);
  p->payload=NULL;
  free(p);
  return 0;
}

int overlay_frame_set_neighbour_as_source(overlay_frame *f,overlay_neighbour *n)
{
  if (!n) return WHY("Neighbour was null");
  bcopy(n->node->sid,f->source,SID_SIZE);
  f->source_address_status=OA_RESOLVED;

  return 0;
}

int overlay_frame_set_neighbour_as_destination(overlay_frame *f,overlay_neighbour *n)
{
  if (!n) return WHY("Neighbour was null");
  bcopy(n->node->sid,f->destination,SID_SIZE);
  f->destination_address_status=OA_RESOLVED;

  return 0;
}

unsigned char *overlay_get_my_sid()
{

  /* Make sure we can find our SID */
  int zero=0;
  if (!findHlr(hlr,&zero,NULL,NULL)) { WHY("Could not find first entry in HLR"); return NULL; }
  return &hlr[zero+4];
}

int overlay_frame_set_me_as_source(overlay_frame *f)
{
  unsigned char *sid=overlay_get_my_sid();
  if (!sid) return WHY("overlay_get_my_sid() failed.");
  bcopy(sid,f->source,SID_SIZE);

  f->source_address_status=OA_RESOLVED;

  return 0;
}

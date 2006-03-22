/*	$KAME: sctp_sys_calls.c,v 1.9 2004/08/17 06:08:53 itojun Exp $ */

/*
 * Copyright (C) 2002-2005 Cisco Systems Inc,
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/sctp_uio.h>
#include <netinet/sctp.h>

#include <net/if_dl.h>

#ifndef IN6_IS_ADDR_V4MAPPED
#define IN6_IS_ADDR_V4MAPPED(a)		      \
	((*(const u_int32_t *)(const void *)(&(a)->s6_addr[0]) == 0) &&	\
	 (*(const u_int32_t *)(const void *)(&(a)->s6_addr[4]) == 0) &&	\
	 (*(const u_int32_t *)(const void *)(&(a)->s6_addr[8]) == ntohl(0x0000ffff)))
#endif

#define SCTP_CONTROL_VEC_SIZE_SND   8192
#define SCTP_CONTROL_VEC_SIZE_RCV  16384

#ifdef SCTP_DEBUG_PRINT_ADDRESS
static void
SCTPPrintAnAddress(struct sockaddr *a)
{
  char stringToPrint[256];
  u_short prt;
  char *srcaddr,*txt;
  if (a == NULL) {
	  printf("NULL\n");
	  return;
  }
  if(a->sa_family == AF_INET){
    srcaddr = (char *)&((struct sockaddr_in *)a)->sin_addr;
    txt = "IPv4 Address: ";
    prt = ntohs(((struct sockaddr_in *)a)->sin_port);
  }else if(a->sa_family == AF_INET6){
    srcaddr = (char *)&((struct sockaddr_in6 *)a)->sin6_addr;
    prt = ntohs(((struct sockaddr_in6 *)a)->sin6_port);
    txt = "IPv6 Address: ";
  }else if(a->sa_family == AF_LINK){
    int i;
    char tbuf[200];
    u_char adbuf[200];
    struct sockaddr_dl *dl;

    dl = (struct sockaddr_dl *)a;
    strncpy(tbuf,dl->sdl_data,dl->sdl_nlen);
    tbuf[dl->sdl_nlen] = 0;
    printf("Intf:%s (len:%d)Interface index:%d type:%x(%d) ll-len:%d ",
	   tbuf,
	   dl->sdl_nlen,
	   dl->sdl_index,
	   dl->sdl_type,
	   dl->sdl_type,
	   dl->sdl_alen
	   );
    memcpy(adbuf,LLADDR(dl),dl->sdl_alen);
    for(i=0;i<dl->sdl_alen;i++){
      printf("%2.2x",adbuf[i]);
      if(i<(dl->sdl_alen-1))
	printf(":");
    }
    printf("\n");
    /*	u_short	sdl_route[16];*/	/* source routing information */
    return;
  }else{
    return;
  }
  if(inet_ntop(a->sa_family,srcaddr,stringToPrint,sizeof(stringToPrint))){
	  if(a->sa_family == AF_INET6){
		  printf("%s%s:%d scope:%d\n",
		      txt,stringToPrint,prt,
		      ((struct sockaddr_in6 *)a)->sin6_scope_id);
	  }else{
		  printf("%s%s:%d\n",txt,stringToPrint,prt);
	  }

  }else{
    printf("%s unprintable?\n",txt);
  }
}
#endif /* SCTP_DEBUG_PRINT_ADDRESS */

static void
in6_sin6_2_sin(struct sockaddr_in *sin, struct sockaddr_in6 *sin6)
{
	bzero(sin, sizeof(*sin));
	sin->sin_len = sizeof(struct sockaddr_in);
	sin->sin_family = AF_INET;
	sin->sin_port = sin6->sin6_port;
	sin->sin_addr.s_addr = sin6->sin6_addr.__u6_addr.__u6_addr32[3];
}

int
sctp_getaddrlen(sa_family_t family)
{
	int error,siz,sd;
	struct sctp_assoc_value av;
	av.assoc_value = family;
	siz = sizeof(av);
#if defined(AF_INET)
	sd = socket(AF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
#elif defined(AF_INET6)
	sd = socket(AF_INET6, SOCK_SEQPACKET, IPPROTO_SCTP);
#endif
	if(sd == -1) {
		return(errno);
	}
	error = getsockopt(sd, IPPROTO_SCTP, SCTP_GET_ADDR_LEN , &av, &siz);
	close(sd);
	if(error == 0) {
		return((int)av.assoc_value);
	} else {
		return(error);
	}
}

int
sctp_connectx(int sd, struct sockaddr *addrs, int addrcnt)
{
	char buf[2048];
	int i,ret,cnt,*aa;
	char *cpto;
	struct sockaddr *at;
	size_t len = sizeof(int);
	
	at = addrs;
	cnt = 0;
	cpto = ((caddr_t)buf + sizeof(int));
	/* validate all the addresses and get the size */
	for (i=0; i < addrcnt; i++) {
		if (at->sa_family == AF_INET) {
			memcpy(cpto, at, at->sa_len);
			cpto = ((caddr_t)cpto + at->sa_len);
			len += at->sa_len;
		} else if (at->sa_family == AF_INET6){
			if(IN6_IS_ADDR_V4MAPPED(&((struct sockaddr_in6 *)at)->sin6_addr)){
				len += sizeof(struct sockaddr_in);
				in6_sin6_2_sin((struct sockaddr_in *)cpto,(struct sockaddr_in6 *)at);
				cpto = ((caddr_t)cpto + sizeof(struct sockaddr_in));
				len += sizeof(struct sockaddr_in);
			} else {
				memcpy(cpto, at, at->sa_len);
				cpto = ((caddr_t)cpto + at->sa_len);
				len += at->sa_len;
			}
		} else {
			errno = EINVAL;
			return (-1);
		}
		if (len > (sizeof(buf)-sizeof(int))) {
			/* Never enough memory */
			return(E2BIG);
		}
		at = (struct sockaddr *)((caddr_t)at + at->sa_len);
		cnt++;
        }
	/* do we have any? */
	if (cnt == 0) {
		errno = EINVAL;
		return(-1);
	}
	aa = (int *)buf;
	*aa = cnt;
	ret = setsockopt(sd, IPPROTO_SCTP, SCTP_CONNECT_X, (void *)buf,
			 (socklen_t)len);
	return (ret);
}

int
sctp_bindx(int sd, struct sockaddr *addrs, int addrcnt, int flags)
{
	struct sctp_getaddresses *gaddrs;
	struct sockaddr *sa;
	int i, sz, fam, argsz;

	if ((flags != SCTP_BINDX_ADD_ADDR) && 
	    (flags != SCTP_BINDX_REM_ADDR)) {
		errno = EFAULT;
		return(-1);
	}
	argsz = (sizeof(struct sockaddr_storage) +
	    sizeof(struct sctp_getaddresses));
	gaddrs = (struct sctp_getaddresses *)calloc(1, argsz);
	if (gaddrs == NULL) {
		errno = ENOMEM;
		return(-1);
	}
	gaddrs->sget_assoc_id = 0;
	sa = addrs;
	for (i = 0; i < addrcnt; i++) {
		sz = sa->sa_len;
		fam = sa->sa_family;
		((struct sockaddr_in *)&addrs[i])->sin_port = ((struct sockaddr_in *)sa)->sin_port;
		if ((fam != AF_INET) && (fam != AF_INET6)) {
			errno = EINVAL;
			return(-1);
		}
		memcpy(gaddrs->addr, sa, sz);
		if (setsockopt(sd, IPPROTO_SCTP, flags, 
			       gaddrs, (socklen_t)argsz) != 0) {
			free(gaddrs);
			return(-1);
		}
		memset(gaddrs, 0, argsz);
		sa = (struct sockaddr *)((caddr_t)sa + sz);
	}
	free(gaddrs);
	return(0);
}


int
sctp_opt_info(int sd, sctp_assoc_t id, int opt, void *arg, socklen_t *size)
{
	if (arg == NULL) {
		return(EINVAL);
	}
	if ((opt == SCTP_RTOINFO) || 
 	    (opt == SCTP_ASSOCINFO) || 
	    (opt == SCTP_PRIMARY_ADDR) || 
	    (opt == SCTP_SET_PEER_PRIMARY_ADDR) || 
	    (opt == SCTP_PEER_ADDR_PARAMS) || 
	    (opt == SCTP_STATUS) || 
	    (opt == SCTP_GET_PEER_ADDR_INFO)) { 
		*(sctp_assoc_t *)arg = id;
		return(getsockopt(sd, IPPROTO_SCTP, opt, arg, size));
	}else{
		errno = EOPNOTSUPP;
		return(-1);
	}
}

int
sctp_getpaddrs(int sd, sctp_assoc_t id, struct sockaddr **raddrs)
{
	struct sctp_getaddresses *addrs;
	struct sockaddr *sa;
	struct sockaddr *re;
	sctp_assoc_t asoc;
	caddr_t lim;
	size_t siz;
	int cnt;

	if (raddrs == NULL) {
		errno = EFAULT;
		return(-1);
	}
	asoc = id;
	siz = sizeof(sctp_assoc_t);  
	if (getsockopt(sd, IPPROTO_SCTP, SCTP_GET_REMOTE_ADDR_SIZE,
	    &asoc, (socklen_t *)&siz) != 0) {
		return(-1);
	}
	siz = (size_t)asoc;
	siz += sizeof(struct sctp_getaddresses);
	addrs = calloc((unsigned long)1, (unsigned long)siz);
	if (addrs == NULL) {
		errno = ENOMEM;
		return(-1);
	}
	memset(addrs, 0, siz);
	addrs->sget_assoc_id = id;
	/* Now lets get the array of addresses */
	if (getsockopt(sd, IPPROTO_SCTP, SCTP_GET_PEER_ADDRESSES,
	    addrs, (socklen_t *)&siz) != 0) {
		free(addrs);
		return(-1);
	}
	re = (struct sockaddr *)&addrs->addr[0];
	*raddrs = re;
	cnt = 0;
	sa = (struct sockaddr *)&addrs->addr[0];
	lim = (caddr_t)addrs + siz;
	while ((caddr_t)sa < lim) {
		cnt++;
		sa = (struct sockaddr *)((caddr_t)sa + sa->sa_len);
		if (sa->sa_len == 0)
			break;
	}
	return(cnt);
}

void sctp_freepaddrs(struct sockaddr *addrs)
{
	/* Take away the hidden association id */
	void *fr_addr;
	fr_addr = (void *)((caddr_t)addrs - sizeof(sctp_assoc_t));
	/* Now free it */
	free(fr_addr);
}

int
sctp_getladdrs (int sd, sctp_assoc_t id, struct sockaddr **raddrs)
{
	struct sctp_getaddresses *addrs;
	struct sockaddr *re;
	caddr_t lim;
	struct sockaddr *sa;
	int size_of_addresses;
	size_t siz;
	int cnt;

	if (raddrs == NULL) {
		errno = EFAULT;
		return(-1);
	}
	size_of_addresses = 0;
	siz = sizeof(int);  
	if (getsockopt(sd, IPPROTO_SCTP, SCTP_GET_LOCAL_ADDR_SIZE,
	    &size_of_addresses, (socklen_t *)&siz) != 0) {
		return(-1);
	}
	if (size_of_addresses == 0) {
		errno = ENOTCONN;
		return(-1);
	}
	siz = size_of_addresses + sizeof(struct sockaddr_storage);
	siz += sizeof(struct sctp_getaddresses);
	addrs = calloc((unsigned long)1, (unsigned long)siz);
	if (addrs == NULL) {
		errno = ENOMEM;
		return(-1);
	}
	memset(addrs, 0, siz);
	addrs->sget_assoc_id = id;
	/* Now lets get the array of addresses */
	if (getsockopt(sd, IPPROTO_SCTP, SCTP_GET_LOCAL_ADDRESSES, addrs,
	    (socklen_t *)&siz) != 0) {
		free(addrs);
		return(-1);
	}
	re = (struct sockaddr *)&addrs->addr[0];
	*raddrs = re;
	cnt = 0;
	sa = (struct sockaddr *)&addrs->addr[0];
	lim = (caddr_t)addrs + siz;
	while ((caddr_t)sa < lim) {
		cnt++;
		sa = (struct sockaddr *)((caddr_t)sa + sa->sa_len);
		if (sa->sa_len == 0)
			break;
	}
	return(cnt);
}

void sctp_freeladdrs(struct sockaddr *addrs)
{
	/* Take away the hidden association id */
	void *fr_addr;
	fr_addr = (void *)((caddr_t)addrs - sizeof(sctp_assoc_t));
	/* Now free it */
	free(fr_addr);
}


#ifndef SYS_sctp_sendmsg
ssize_t
sctp_sendmsg(int s, 
	     const void *data, 
	     size_t len,
	     const struct sockaddr *to,
	     socklen_t tolen __attribute__((unused)),
	     u_int32_t ppid,
	     u_int32_t flags,
	     u_int16_t stream_no,
	     u_int32_t timetolive,
	     u_int32_t context)
{

	ssize_t sz;
	struct msghdr msg;
	struct sctp_sndrcvinfo *s_info;
	struct iovec iov[2];
	char controlVector[SCTP_CONTROL_VEC_SIZE_RCV];
	struct cmsghdr *cmsg;
	struct sockaddr *who=NULL;
	union {
		struct sockaddr_in in;
		struct sockaddr_in6 in6;
	}addr;

/*
  fprintf(io, "sctp_sendmsg(sd:%d, data:%x, len:%d, to:%x, tolen:%d, ppid:%x, flags:%x str:%d ttl:%d ctx:%x\n",
  s, 
  (u_int)data, 
  (int)len,
  (u_int)to,
  (int)tolen,
  ppid, flags,
  (int)stream_no,
  (int)timetolive,
  (u_int)context);
  fflush(io);
*/
	if (to) {
		if (to->sa_len == 0) {
			/* For the lazy app, that did not
			 * set sa_len, we attempt to set for them.
			 */
			if(to->sa_family == AF_INET){
				memcpy(&addr, to, sizeof(struct sockaddr_in));
				addr.in.sin_len = sizeof(struct sockaddr_in);
			} else if(to->sa_family == AF_INET6){
				memcpy(&addr, to, sizeof(struct sockaddr_in6));
				addr.in6.sin6_len = sizeof(struct sockaddr_in6);
			}
		} else {
			memcpy (&addr, to, to->sa_len);
		}
		who = (struct sockaddr *)&addr;
	}
	iov[0].iov_base = (char *)data;
	iov[0].iov_len = len;
	iov[1].iov_base = NULL;
	iov[1].iov_len = 0;

	if (to) {
		msg.msg_name = (caddr_t)who;
		msg.msg_namelen = who->sa_len;
	} else {
		msg.msg_name = (caddr_t)NULL;
		msg.msg_namelen = 0;
	}
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = (caddr_t)controlVector;

	cmsg = (struct cmsghdr *)controlVector;

	cmsg->cmsg_level = IPPROTO_SCTP;
	cmsg->cmsg_type = SCTP_SNDRCV;
	cmsg->cmsg_len = CMSG_LEN (sizeof(struct sctp_sndrcvinfo) );
	s_info = (struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);

	s_info->sinfo_stream = stream_no;
	s_info->sinfo_ssn = 0;
	s_info->sinfo_flags = flags;
	s_info->sinfo_ppid = ppid;
	s_info->sinfo_context = context;
	s_info->sinfo_assoc_id = 0;
	s_info->sinfo_timetolive = timetolive;
	errno = 0;
	msg.msg_controllen = cmsg->cmsg_len;
	sz = sendmsg(s, &msg, 0);
	return(sz);

}
#endif

sctp_assoc_t
sctp_getassocid(int sd, struct sockaddr *sa)
{
  struct sctp_paddrparams sp;
  size_t siz;

  /* First get the assoc id */
  siz = sizeof(struct sctp_paddrparams);
  memset(&sp,0,sizeof(sp));
  memcpy((caddr_t)&sp.spp_address,sa,sa->sa_len);
  errno = 0;
  if(getsockopt(sd,IPPROTO_SCTP,
		SCTP_PEER_ADDR_PARAMS, &sp, (socklen_t *)&siz) != 0) {	
	  return((sctp_assoc_t)0);
  }
  /* We depend on the fact that 0 can never be returned */
  return(sp.spp_assoc_id);
}

#ifndef SYS_sctp_send
ssize_t
sctp_send(int sd, const void *data, size_t len,
	  const struct sctp_sndrcvinfo *sinfo,
	  int flags)
{

	ssize_t sz;
	struct msghdr msg;
	struct iovec iov[2];
	struct sctp_sndrcvinfo *s_info;
	char controlVector[SCTP_CONTROL_VEC_SIZE_SND];
	struct cmsghdr *cmsg;

	if (sinfo == NULL) {
	    return(EINVAL);
	}
	iov[0].iov_base = (char *)data;
	iov[0].iov_len = len;
	iov[1].iov_base = NULL;
	iov[1].iov_len = 0;

	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = (caddr_t)controlVector;
  
	cmsg = (struct cmsghdr *)controlVector;

	cmsg->cmsg_level = IPPROTO_SCTP;
	cmsg->cmsg_type = SCTP_SNDRCV;
	cmsg->cmsg_len = CMSG_LEN (sizeof(struct sctp_sndrcvinfo) );
	s_info = (struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);
	/* copy in the data */
	*s_info = *sinfo;
	errno = 0;
	msg.msg_controllen = cmsg->cmsg_len;
	sz = sendmsg(sd, &msg, flags);
	return(sz);

}
#endif


ssize_t
sctp_sendx(int sd, const void *msg, size_t msg_len, 
	   struct sockaddr *addrs, int addrcnt,
	   struct sctp_sndrcvinfo *sinfo,
	   int flags)
{
	ssize_t ret;
	int i, cnt, *aa, saved_errno;
	char *buf;
	int add_len, len, no_end_cx=0;
	struct sockaddr *at;
	
	len = sizeof(int);
	at = addrs;
	cnt = 0;
	/* validate all the addresses and get the size */
	for (i=0; i < addrcnt; i++) {
		if (at->sa_family == AF_INET) {
			add_len = sizeof(struct sockaddr_in);
		} else if (at->sa_family == AF_INET6) {
			add_len = sizeof(struct sockaddr_in6);
		} else {
			errno = EINVAL;
			return (-1);
		}
		len += add_len;
		at = (struct sockaddr *)((caddr_t)at + add_len);
		cnt++;
	}
	/* do we have any? */
	if (cnt == 0) {
		errno = EINVAL;
		return(-1);
	}
	if (len > 2048) {
		/* Never enough memory */
		return(E2BIG);
	}
	buf = malloc(len);
	if(buf == NULL) {
		return(ENOMEM);
	}
	aa = (int *)buf;
	*aa = cnt;
	aa++;
	memcpy((caddr_t)aa,addrs,(len-sizeof(int)));
	ret = setsockopt(sd, IPPROTO_SCTP, SCTP_CONNECT_X_DELAYED, (void *)buf,
			 (socklen_t)len);

	free(buf);
	if(ret != 0) {
		if(errno == EALREADY) {
			no_end_cx = 1;;
			goto continue_send;
		}
		return(ret);
	}
 continue_send:
	sinfo->sinfo_assoc_id = sctp_getassocid(sd, addrs);
	if (sinfo->sinfo_assoc_id == 0) {
		printf("Huh, can't get associd? TSNH!\n");
		(void)setsockopt(sd, IPPROTO_SCTP, SCTP_CONNECT_X_COMPLETE, (void *)addrs,
				 (socklen_t)addrs->sa_len);
		errno = ENOENT;
		return (-1);
	}
	ret = sctp_send(sd, msg, msg_len, sinfo, flags);
	saved_errno = errno;
	if(no_end_cx == 0) 
		(void)setsockopt(sd, IPPROTO_SCTP, SCTP_CONNECT_X_COMPLETE, (void *)addrs,
				 (socklen_t)addrs->sa_len);

	errno = saved_errno;
	return (ret);
}

ssize_t
sctp_sendmsgx(int sd, 
	      const void *msg, 
	      size_t len,
	      struct sockaddr *addrs,
	      int addrcnt,
	      u_int32_t ppid,
	      u_int32_t flags,
	      u_int16_t stream_no,
	      u_int32_t timetolive,
	      u_int32_t context)
{
	struct sctp_sndrcvinfo sinfo;
    
	memset((void *) &sinfo, 0, sizeof(struct sctp_sndrcvinfo));
	sinfo.sinfo_ppid       = ppid;
	sinfo.sinfo_flags      = flags;
	sinfo.sinfo_ssn        = stream_no;
	sinfo.sinfo_timetolive = timetolive;
	sinfo.sinfo_context    = context;
	return sctp_sendx(sd, msg, len, addrs, addrcnt, &sinfo, 0);
}

#ifndef SYS_sctp_recvmsg
ssize_t
sctp_recvmsg (int s, 
	      void *dbuf, 
	      size_t len,
	      struct sockaddr *from,
	      socklen_t *fromlen,
	      struct sctp_sndrcvinfo *sinfo,
	      int *msg_flags)
{

	struct sctp_sndrcvinfo *s_info;
	ssize_t sz;
	int sinfo_found=0;
	struct msghdr msg;
	struct iovec iov[2];
	char controlVector[SCTP_CONTROL_VEC_SIZE_RCV];
	struct cmsghdr *cmsg;

	if (msg_flags == NULL) {
		errno = EINVAL;
		return (-1);
	}
	msg.msg_flags = 0;
	iov[0].iov_base = dbuf;
	iov[0].iov_len = len;
	iov[1].iov_base = NULL;
	iov[1].iov_len = 0;
	msg.msg_name = (caddr_t)from;
	if (fromlen == NULL)
		msg.msg_namelen = 0;
	else
		msg.msg_namelen = *fromlen;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = (caddr_t)controlVector;
	msg.msg_controllen = sizeof(controlVector);
	errno = 0;
	sz = recvmsg(s,&msg,0);
	if(sz <= 0)
		return(sz);

	s_info = NULL;
	len = sz;
	*msg_flags = msg.msg_flags;
	if(sinfo) 
		sinfo->sinfo_assoc_id = 0;

	if ((msg.msg_controllen) && sinfo) {
		/* parse through and see if we find
		 * the sctp_sndrcvinfo (if the user wants it).
		 */
		cmsg = (struct cmsghdr *)controlVector;
		while (cmsg) {
			if((cmsg->cmsg_len == 0) || (cmsg->cmsg_len > msg.msg_controllen)) {
				break;
			}
			if (cmsg->cmsg_level == IPPROTO_SCTP) {
				if (cmsg->cmsg_type == SCTP_SNDRCV) {
					/* Got it */
					s_info = (struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);
					/* Copy it to the user */
					if(sinfo)
						*sinfo = *s_info;
					sinfo_found = 1;
					break;
				}
			}
			cmsg = CMSG_NXTHDR(&msg,cmsg);
		}
	}
	return(sz);
}
#endif

#if defined(HAVE_SCTP_PEELOFF_SOCKOPT)
#include <netinet/sctp_peeloff.h>

int
sctp_peeloff(int sd, sctp_assoc_t assoc_id)
{
	struct sctp_peeloff_opt peeloff;
	int error;
	int optlen;

	/* set in the socket option params */
	memset(&peeloff, 0, sizeof(peeloff));
	peeloff.s = sd;
	peeloff.assoc_id = assoc_id;
	optlen = sizeof(peeloff);
	error = getsockopt(sd, IPPROTO_SCTP, SCTP_PEELOFF, (void *)&peeloff,
			   &optlen);
	if (error) {
	    errno = error;
	    return (-1);
	} else {
	    return (peeloff.new_sd);
	}
}
#endif

#if !defined(SYS_sctp_peeloff) && !defined(HAVE_SCTP_PEELOFF_SOCKOPT)

int
sctp_peeloff(int sd, sctp_assoc_t assoc_id)
{
	/* NOT supported, return invalid sd */
	errno = ENOTSUP;
	return (-1);
}
#endif

/*-
 * Copyright (c) 2001-2008, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __FreeBSD__
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: head/sys/netinet/sctp_usrreq.c 257555 2013-11-02 20:12:19Z tuexen $");
#endif

#include <netinet/sctp_os.h>
#ifdef __FreeBSD__
#include <sys/proc.h>
#endif
#include <netinet/sctp_pcb.h>
#include <netinet/sctp_header.h>
#include <netinet/sctp_var.h>
#ifdef INET6
#if defined(__Userspace_os_FreeBSD)
#include <netinet6/sctp6_var.h>
#endif
#endif
#include <netinet/sctp_sysctl.h>
#include <netinet/sctp_output.h>
#include <netinet/sctp_uio.h>
#include <netinet/sctp_asconf.h>
#include <netinet/sctputil.h>
#include <netinet/sctp_indata.h>
#include <netinet/sctp_timer.h>
#include <netinet/sctp_auth.h>
#include <netinet/sctp_bsd_addr.h>
#if !defined(__Userspace_os_Windows)
#include <netinet/udp.h>
#endif

#if defined(HAVE_SCTP_PEELOFF_SOCKOPT)
#include <netinet/sctp_peeloff.h>
#endif				/* HAVE_SCTP_PEELOFF_SOCKOPT */

#if defined(__APPLE__)
#define APPLE_FILE_NO 7
#endif

extern struct sctp_cc_functions sctp_cc_functions[];
extern struct sctp_ss_functions sctp_ss_functions[];

void
#if defined(__Userspace__)
sctp_init(uint16_t port,
          int (*conn_output)(void *addr, void *buffer, size_t length, uint8_t tos, uint8_t set_df),
          void (*debug_printf)(const char *format, ...))
#else
sctp_init(void)
#endif
{
#if !defined(__Panda__) && !defined(__Userspace__)
	u_long sb_max_adj;

#endif
#if defined(__Userspace_os_Windows)
#if defined(INET) || defined(INET6)
	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
		SCTP_PRINTF("WSAStartup failed\n");
		exit (-1);
	}
#endif
	InitializeConditionVariable(&accept_cond);
	InitializeCriticalSection(&accept_mtx);
#endif
	/* Initialize and modify the sysctled variables */
	sctp_init_sysctls();
#if defined(__Userspace__)
#if defined(__Userspace_os_Windows)
	srand((unsigned int)time(NULL));
#else
	srandom(getpid()); /* so inp->sctp_ep.random_numbers are truly random... */
#endif
#endif
#if defined(__Panda__)
	sctp_sendspace = SB_MAX;
	sctp_recvspace = SB_MAX;

#elif defined(__Userspace__)
	SCTP_BASE_SYSCTL(sctp_sendspace) = SB_MAX;
	SCTP_BASE_SYSCTL(sctp_recvspace) = SB_RAW;
	SCTP_BASE_SYSCTL(sctp_udp_tunneling_port) = port;
#else
#if !defined(__APPLE__)
	if ((nmbclusters / 8) > SCTP_ASOC_MAX_CHUNKS_ON_QUEUE)
		SCTP_BASE_SYSCTL(sctp_max_chunks_on_queue) = (nmbclusters / 8);
#endif
	/*
	 * Allow a user to take no more than 1/2 the number of clusters or
	 * the SB_MAX whichever is smaller for the send window.
	 */
#if defined(__APPLE__)
	sb_max_adj = (u_long)((u_quad_t) (sb_max) * MCLBYTES / (MSIZE + MCLBYTES));
#else
	sb_max_adj = (u_long)((u_quad_t) (SB_MAX) * MCLBYTES / (MSIZE + MCLBYTES));
#endif
#if defined(__APPLE__)
	SCTP_BASE_SYSCTL(sctp_sendspace) = sb_max_adj;
#else
	SCTP_BASE_SYSCTL(sctp_sendspace) = min(sb_max_adj,
	    (((uint32_t)nmbclusters / 2) * SCTP_DEFAULT_MAXSEGMENT));
#endif
	/*
	 * Now for the recv window, should we take the same amount? or
	 * should I do 1/2 the SB_MAX instead in the SB_MAX min above. For
	 * now I will just copy.
	 */
	SCTP_BASE_SYSCTL(sctp_recvspace) = SCTP_BASE_SYSCTL(sctp_sendspace);
#endif
	SCTP_BASE_VAR(first_time) = 0;
	SCTP_BASE_VAR(sctp_pcb_initialized) = 0;
#if defined(__Userspace__)
#if !defined(__Userspace_os_Windows)
#if defined(INET) || defined(INET6)
	SCTP_BASE_VAR(userspace_route) = -1;
#endif
#endif
#ifdef INET
	SCTP_BASE_VAR(userspace_rawsctp) = -1;
	SCTP_BASE_VAR(userspace_udpsctp) = -1;
#endif
#ifdef INET6
	SCTP_BASE_VAR(userspace_rawsctp6) = -1;
	SCTP_BASE_VAR(userspace_udpsctp6) = -1;
#endif
	SCTP_BASE_VAR(timer_thread_should_exit) = 0;
	SCTP_BASE_VAR(conn_output) = conn_output;
	SCTP_BASE_VAR(debug_printf) = debug_printf;
#endif
	sctp_pcb_init();
#if defined(__Userspace__)
	sctp_start_timer();
#endif
#if defined(SCTP_PACKET_LOGGING)
	SCTP_BASE_VAR(packet_log_writers) = 0;
	SCTP_BASE_VAR(packet_log_end) = 0;
	bzero(&SCTP_BASE_VAR(packet_log_buffer), SCTP_PACKET_LOG_SIZE);
#endif
#if defined(__APPLE__)
	SCTP_BASE_VAR(sctp_main_timer_ticks) = 0;
	sctp_start_main_timer();
	sctp_address_monitor_start();
	sctp_over_udp_start();
#endif
}

void
sctp_finish(void)
{
#if defined(__APPLE__)
	sctp_over_udp_stop();
	sctp_address_monitor_stop();
	sctp_stop_main_timer();
#endif
#if defined(__Userspace__)
#if defined(INET) || defined(INET6)
	recv_thread_destroy();
#endif
#if !defined(__Userspace_os_Windows)
#if defined(INET) || defined(INET6)
	if (SCTP_BASE_VAR(userspace_route) != -1) {
		pthread_join(SCTP_BASE_VAR(recvthreadroute), NULL);
	}
#endif
#endif
#ifdef INET
	if (SCTP_BASE_VAR(userspace_rawsctp) != -1) {
#if defined(__Userspace_os_Windows)
		WaitForSingleObject(SCTP_BASE_VAR(recvthreadraw), INFINITE);
#else
		pthread_join(SCTP_BASE_VAR(recvthreadraw), NULL);
#endif
	}
	if (SCTP_BASE_VAR(userspace_udpsctp) != -1) {
#if defined(__Userspace_os_Windows)
		WaitForSingleObject(SCTP_BASE_VAR(recvthreadudp), INFINITE);
#else
		pthread_join(SCTP_BASE_VAR(recvthreadudp), NULL);
#endif
	}
#endif
#ifdef INET6
	if (SCTP_BASE_VAR(userspace_rawsctp6) != -1) {
#if defined(__Userspace_os_Windows)
		WaitForSingleObject(SCTP_BASE_VAR(recvthreadraw6), INFINITE);
#else
		pthread_join(SCTP_BASE_VAR(recvthreadraw6), NULL);
#endif
	}
	if (SCTP_BASE_VAR(userspace_udpsctp6) != -1) {
#if defined(__Userspace_os_Windows)
		WaitForSingleObject(SCTP_BASE_VAR(recvthreadudp6), INFINITE);
#else
		pthread_join(SCTP_BASE_VAR(recvthreadudp6), NULL);
#endif
	}
#endif
	SCTP_BASE_VAR(timer_thread_should_exit) = 1;
#if defined(__Userspace_os_Windows)
	WaitForSingleObject(SCTP_BASE_VAR(timer_thread), INFINITE);
#else
	pthread_join(SCTP_BASE_VAR(timer_thread), NULL);
#endif
#endif
	sctp_pcb_finish();
#if defined(__Windows__)
	sctp_finish_sysctls();
#if defined(INET) || defined(INET6)
	WSACleanup();
#endif
#endif
}



void
sctp_pathmtu_adjustment(struct sctp_tcb *stcb, uint16_t nxtsz)
{
	struct sctp_tmit_chunk *chk;
	uint16_t overhead;

	/* Adjust that too */
	stcb->asoc.smallest_mtu = nxtsz;
	/* now off to subtract IP_DF flag if needed */
	overhead = IP_HDR_SIZE;
	if (sctp_auth_is_required_chunk(SCTP_DATA, stcb->asoc.peer_auth_chunks)) {
		overhead += sctp_get_auth_chunk_len(stcb->asoc.peer_hmac_id);
	}
	TAILQ_FOREACH(chk, &stcb->asoc.send_queue, sctp_next) {
		if ((chk->send_size + overhead) > nxtsz) {
			chk->flags |= CHUNK_FLAGS_FRAGMENT_OK;
		}
	}
	TAILQ_FOREACH(chk, &stcb->asoc.sent_queue, sctp_next) {
		if ((chk->send_size + overhead) > nxtsz) {
			/*
			 * For this guy we also mark for immediate resend
			 * since we sent to big of chunk
			 */
			chk->flags |= CHUNK_FLAGS_FRAGMENT_OK;
			if (chk->sent < SCTP_DATAGRAM_RESEND) {
				sctp_flight_size_decrease(chk);
				sctp_total_flight_decrease(stcb, chk);
			}
			if (chk->sent != SCTP_DATAGRAM_RESEND) {
				sctp_ucount_incr(stcb->asoc.sent_queue_retran_cnt);
			}
			chk->sent = SCTP_DATAGRAM_RESEND;
			chk->rec.data.doing_fast_retransmit = 0;
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FLIGHT_LOGGING_ENABLE) {
				sctp_misc_ints(SCTP_FLIGHT_LOG_DOWN_PMTU,
					       chk->whoTo->flight_size,
					       chk->book_size,
					       (uintptr_t)chk->whoTo,
					       chk->rec.data.TSN_seq);
			}
			/* Clear any time so NO RTT is being done */
			chk->do_rtt = 0;
		}
	}
}

#ifdef INET
#if !defined(__Userspace__)
#if defined(__Panda__) || defined(__Windows__)
void
#else
static void
#endif
sctp_notify_mbuf(struct sctp_inpcb *inp,
    struct sctp_tcb *stcb,
    struct sctp_nets *net,
    struct ip *ip,
    struct sctphdr *sh)
{
	struct icmp *icmph;
	int totsz, tmr_stopped = 0;
	uint16_t nxtsz;

	/* protection */
	if ((inp == NULL) || (stcb == NULL) || (net == NULL) ||
	    (ip == NULL) || (sh == NULL)) {
		if (stcb != NULL) {
			SCTP_TCB_UNLOCK(stcb);
		}
		return;
	}
	/* First job is to verify the vtag matches what I would send */
	if (ntohl(sh->v_tag) != (stcb->asoc.peer_vtag)) {
		SCTP_TCB_UNLOCK(stcb);
		return;
	}
	icmph = (struct icmp *)((caddr_t)ip - (sizeof(struct icmp) -
	    sizeof(struct ip)));
	if (icmph->icmp_type != ICMP_UNREACH) {
		/* We only care about unreachable */
		SCTP_TCB_UNLOCK(stcb);
		return;
	}
	if (icmph->icmp_code != ICMP_UNREACH_NEEDFRAG) {
		/* not a unreachable message due to frag. */
		SCTP_TCB_UNLOCK(stcb);
		return;
	}
#if defined(__FreeBSD__) && __FreeBSD_version >= 1000000
	totsz = ntohs(ip->ip_len);
#else
	totsz = ip->ip_len;
#endif

	nxtsz = ntohs(icmph->icmp_nextmtu);
	if (nxtsz == 0) {
		/*
		 * old type router that does not tell us what the next size
		 * mtu is. Rats we will have to guess (in a educated fashion
		 * of course)
		 */
		nxtsz = sctp_get_prev_mtu(totsz);
	}
	/* Stop any PMTU timer */
	if (SCTP_OS_TIMER_PENDING(&net->pmtu_timer.timer)) {
		tmr_stopped = 1;
		sctp_timer_stop(SCTP_TIMER_TYPE_PATHMTURAISE, inp, stcb, net,
				SCTP_FROM_SCTP_USRREQ+SCTP_LOC_1);
	}
	/* Adjust destination size limit */
	if (net->mtu > nxtsz) {
		net->mtu = nxtsz;
		if (net->port) {
			net->mtu -= sizeof(struct udphdr);
		}
	}
	/* now what about the ep? */
	if (stcb->asoc.smallest_mtu > nxtsz) {
		sctp_pathmtu_adjustment(stcb, nxtsz);
	}
	if (tmr_stopped)
		sctp_timer_start(SCTP_TIMER_TYPE_PATHMTURAISE, inp, stcb, net);

	SCTP_TCB_UNLOCK(stcb);
}
#endif
#endif

void
sctp_notify(struct sctp_inpcb *inp,
    struct ip *ip,
    struct sctphdr *sh,
    struct sockaddr *to,
    struct sctp_tcb *stcb,
    struct sctp_nets *net)
{
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;

#endif
	struct icmp *icmph;

	/* protection */
	if ((inp == NULL) || (stcb == NULL) || (net == NULL) ||
	    (sh == NULL) || (to == NULL)) {
		if (stcb)
			SCTP_TCB_UNLOCK(stcb);
		return;
	}
	/* First job is to verify the vtag matches what I would send */
	if (ntohl(sh->v_tag) != (stcb->asoc.peer_vtag)) {
		SCTP_TCB_UNLOCK(stcb);
		return;
	}

	icmph = (struct icmp *)((caddr_t)ip - (sizeof(struct icmp) -
					       sizeof(struct ip)));
	if (icmph->icmp_type != ICMP_UNREACH) {
		/* We only care about unreachable */
		SCTP_TCB_UNLOCK(stcb);
		return;
	}
	if ((icmph->icmp_code == ICMP_UNREACH_NET) ||
	    (icmph->icmp_code == ICMP_UNREACH_HOST) ||
	    (icmph->icmp_code == ICMP_UNREACH_NET_UNKNOWN) ||
	    (icmph->icmp_code == ICMP_UNREACH_HOST_UNKNOWN) ||
	    (icmph->icmp_code == ICMP_UNREACH_ISOLATED) ||
	    (icmph->icmp_code == ICMP_UNREACH_NET_PROHIB) ||
	    (icmph->icmp_code == ICMP_UNREACH_HOST_PROHIB) ||
#ifdef __Panda__
	    (icmph->icmp_code == ICMP_UNREACH_ADMIN)) {
#else
	    (icmph->icmp_code == ICMP_UNREACH_FILTER_PROHIB)) {
#endif

		/*
		 * Hmm reachablity problems we must examine closely. If its
		 * not reachable, we may have lost a network. Or if there is
		 * NO protocol at the other end named SCTP. well we consider
		 * it a OOTB abort.
		 */
		if (net->dest_state & SCTP_ADDR_REACHABLE) {
			/* Ok that destination is NOT reachable */
			net->dest_state &= ~SCTP_ADDR_REACHABLE;
			net->dest_state &= ~SCTP_ADDR_PF;
			sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_DOWN,
					stcb, 0,
					(void *)net, SCTP_SO_NOT_LOCKED);
		}
		SCTP_TCB_UNLOCK(stcb);
	} else  if ((icmph->icmp_code == ICMP_UNREACH_PROTOCOL) ||
		    (icmph->icmp_code == ICMP_UNREACH_PORT)) {
		/*
		 * Here the peer is either playing tricks on us,
		 * including an address that belongs to someone who
		 * does not support SCTP OR was a userland
		 * implementation that shutdown and now is dead. In
		 * either case treat it like a OOTB abort with no
		 * TCB
		 */
		sctp_abort_notification(stcb, 1, 0, NULL, SCTP_SO_NOT_LOCKED);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		so = SCTP_INP_SO(inp);
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
		(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC, SCTP_FROM_SCTP_USRREQ+SCTP_LOC_2);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
		/* SCTP_TCB_UNLOCK(stcb); MT: I think this is not needed.*/
#endif
		/* no need to unlock here, since the TCB is gone */
	} else {
		SCTP_TCB_UNLOCK(stcb);
	}
}

#ifdef INET
#if !defined(__Panda__) && !defined(__Userspace__)
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
void
#else
void *
#endif
sctp_ctlinput(cmd, sa, vip)
	int cmd;
	struct sockaddr *sa;
	void *vip;
{
	struct ip *ip = vip;
	struct sctphdr *sh;
	uint32_t vrf_id;
	/* FIX, for non-bsd is this right? */
	vrf_id = SCTP_DEFAULT_VRFID;
	if (sa->sa_family != AF_INET ||
	    ((struct sockaddr_in *)sa)->sin_addr.s_addr == INADDR_ANY) {
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
		return;
#else
		return (NULL);
#endif
	}
	if (PRC_IS_REDIRECT(cmd)) {
		ip = 0;
	} else if ((unsigned)cmd >= PRC_NCMDS || inetctlerrmap[cmd] == 0) {
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
		return;
#else
		return (NULL);
#endif
	}
	if (ip) {
		struct sctp_inpcb *inp = NULL;
		struct sctp_tcb *stcb = NULL;
		struct sctp_nets *net = NULL;
		struct sockaddr_in to, from;

		sh = (struct sctphdr *)((caddr_t)ip + (ip->ip_hl << 2));
		bzero(&to, sizeof(to));
		bzero(&from, sizeof(from));
		from.sin_family = to.sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
		from.sin_len = to.sin_len = sizeof(to);
#endif
		from.sin_port = sh->src_port;
		from.sin_addr = ip->ip_src;
		to.sin_port = sh->dest_port;
		to.sin_addr = ip->ip_dst;

		/*
		 * 'to' holds the dest of the packet that failed to be sent.
		 * 'from' holds our local endpoint address. Thus we reverse
		 * the to and the from in the lookup.
		 */
		stcb = sctp_findassociation_addr_sa((struct sockaddr *)&to,
		    (struct sockaddr *)&from,
		    &inp, &net, 1, vrf_id);
		if (stcb != NULL && inp && (inp->sctp_socket != NULL)) {
			if (cmd != PRC_MSGSIZE) {
				sctp_notify(inp, ip, sh,
				    (struct sockaddr *)&to, stcb,
				    net);
			} else {
				/* handle possible ICMP size messages */
				sctp_notify_mbuf(inp, stcb, net, ip, sh);
			}
		} else {
#if defined(__FreeBSD__) && __FreeBSD_version < 500000
			/*
			 * XXX must be fixed for 5.x and higher, leave for
			 * 4.x
			 */
			if (PRC_IS_REDIRECT(cmd) && inp) {
				in_rtchange((struct inpcb *)inp,
				    inetctlerrmap[cmd]);
			}
#endif
			if ((stcb == NULL) && (inp != NULL)) {
				/* reduce ref-count */
				SCTP_INP_WLOCK(inp);
				SCTP_INP_DECR_REF(inp);
				SCTP_INP_WUNLOCK(inp);
			}
			if (stcb) {
				SCTP_TCB_UNLOCK(stcb);
			}
		}
	}
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
	return;
#else
	return (NULL);
#endif
}
#endif
#endif

#if defined(__FreeBSD__)
static int
sctp_getcred(SYSCTL_HANDLER_ARGS)
{
	struct xucred xuc;
	struct sockaddr_in addrs[2];
	struct sctp_inpcb *inp;
	struct sctp_nets *net;
	struct sctp_tcb *stcb;
	int error;
	uint32_t vrf_id;

	/* FIX, for non-bsd is this right? */
	vrf_id = SCTP_DEFAULT_VRFID;

#if __FreeBSD_version > 602000
	error = priv_check(req->td, PRIV_NETINET_GETCRED);

#elif __FreeBSD_version >= 500000
	error = suser(req->td);
#else
	error = suser(req->p);
#endif
	if (error)
		return (error);

	error = SYSCTL_IN(req, addrs, sizeof(addrs));
	if (error)
		return (error);

	stcb = sctp_findassociation_addr_sa(sintosa(&addrs[1]),
	    sintosa(&addrs[0]),
	    &inp, &net, 1, vrf_id);
	if (stcb == NULL || inp == NULL || inp->sctp_socket == NULL) {
		if ((inp != NULL) && (stcb == NULL)) {
			/* reduce ref-count */
			SCTP_INP_WLOCK(inp);
			SCTP_INP_DECR_REF(inp);
			goto cred_can_cont;
		}

		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOENT);
		error = ENOENT;
		goto out;
	}
	SCTP_TCB_UNLOCK(stcb);
	/* We use the write lock here, only
	 * since in the error leg we need it.
	 * If we used RLOCK, then we would have
	 * to wlock/decr/unlock/rlock. Which
	 * in theory could create a hole. Better
	 * to use higher wlock.
	 */
	SCTP_INP_WLOCK(inp);
 cred_can_cont:
	error = cr_canseesocket(req->td->td_ucred, inp->sctp_socket);
	if (error) {
		SCTP_INP_WUNLOCK(inp);
		goto out;
	}
	cru2x(inp->sctp_socket->so_cred, &xuc);
	SCTP_INP_WUNLOCK(inp);
	error = SYSCTL_OUT(req, &xuc, sizeof(struct xucred));
out:
	return (error);
}

SYSCTL_PROC(_net_inet_sctp, OID_AUTO, getcred, CTLTYPE_OPAQUE | CTLFLAG_RW,
    0, 0, sctp_getcred, "S,ucred", "Get the ucred of a SCTP connection");
#endif				/* #if defined(__FreeBSD__) */


#ifdef INET
#if defined(__Panda__) || defined(__Windows__) || defined(__Userspace__)
int
#elif defined(__FreeBSD__) && __FreeBSD_version > 690000
static void
#else
static int
#endif
sctp_abort(struct socket *so)
{
	struct sctp_inpcb *inp;
	uint32_t flags;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
#if defined(__FreeBSD__) && __FreeBSD_version > 690000
		return;
#else
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (EINVAL);
#endif
	}

 sctp_must_try_again:
	flags = inp->sctp_flags;
#ifdef SCTP_LOG_CLOSING
	sctp_log_closing(inp, NULL, 17);
#endif
	if (((flags & SCTP_PCB_FLAGS_SOCKET_GONE) == 0) &&
	    (atomic_cmpset_int(&inp->sctp_flags, flags, (flags | SCTP_PCB_FLAGS_SOCKET_GONE | SCTP_PCB_FLAGS_CLOSE_IP)))) {
#ifdef SCTP_LOG_CLOSING
		sctp_log_closing(inp, NULL, 16);
#endif
		sctp_inpcb_free(inp, SCTP_FREE_SHOULD_USE_ABORT,
				SCTP_CALLED_AFTER_CMPSET_OFCLOSE);
		SOCK_LOCK(so);
		SCTP_SB_CLEAR(so->so_snd);
		/* same for the rcv ones, they are only
		 * here for the accounting/select.
		 */
		SCTP_SB_CLEAR(so->so_rcv);

#if defined(__APPLE__)
		so->so_usecount--;
#else
		/* Now null out the reference, we are completely detached. */
		so->so_pcb = NULL;
#endif
		SOCK_UNLOCK(so);
	} else {
		flags = inp->sctp_flags;
		if ((flags & SCTP_PCB_FLAGS_SOCKET_GONE) == 0) {
			goto sctp_must_try_again;
		}
	}
#if defined(__FreeBSD__) && __FreeBSD_version > 690000
	return;
#else
	return (0);
#endif
}

#if defined(__Panda__) || defined(__Userspace__)
int
#else
static int
#endif
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
sctp_attach(struct socket *so, int proto SCTP_UNUSED, struct thread *p SCTP_UNUSED)
#elif defined(__Panda__) || defined(__Userspace__)
sctp_attach(struct socket *so, int proto SCTP_UNUSED, uint32_t vrf_id)
#elif defined(__Windows__)
sctp_attach(struct socket *so, int proto SCTP_UNUSED, PKTHREAD p SCTP_UNUSED)
#else
sctp_attach(struct socket *so, int proto SCTP_UNUSED, struct proc *p SCTP_UNUSED)
#endif
{
	struct sctp_inpcb *inp;
	struct inpcb *ip_inp;
	int error;
#if !defined(__Panda__) && !defined(__Userspace__)
	uint32_t vrf_id = SCTP_DEFAULT_VRFID;
#endif
#ifdef IPSEC
	uint32_t flags;
#endif

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp != 0) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (EINVAL);
	}
	if (so->so_snd.sb_hiwat == 0 || so->so_rcv.sb_hiwat == 0) {
		error = SCTP_SORESERVE(so, SCTP_BASE_SYSCTL(sctp_sendspace), SCTP_BASE_SYSCTL(sctp_recvspace));
		if (error) {
			return (error);
		}
	}
	error = sctp_inpcb_alloc(so, vrf_id);
	if (error) {
		return (error);
	}
	inp = (struct sctp_inpcb *)so->so_pcb;
	SCTP_INP_WLOCK(inp);
	inp->sctp_flags &= ~SCTP_PCB_FLAGS_BOUND_V6;	/* I'm not v6! */
	ip_inp = &inp->ip_inp.inp;
	ip_inp->inp_vflag |= INP_IPV4;
	ip_inp->inp_ip_ttl = MODULE_GLOBAL(ip_defttl);
#ifdef IPSEC
#if !(defined(__APPLE__))
	error = ipsec_init_policy(so, &ip_inp->inp_sp);
#ifdef SCTP_LOG_CLOSING
	sctp_log_closing(inp, NULL, 17);
#endif
	if (error != 0) {
	try_again:
		flags = inp->sctp_flags;
		if (((flags & SCTP_PCB_FLAGS_SOCKET_GONE) == 0) &&
		    (atomic_cmpset_int(&inp->sctp_flags, flags, (flags | SCTP_PCB_FLAGS_SOCKET_GONE | SCTP_PCB_FLAGS_CLOSE_IP)))) {
#ifdef SCTP_LOG_CLOSING
			sctp_log_closing(inp, NULL, 15);
#endif
			SCTP_INP_WUNLOCK(inp);
			sctp_inpcb_free(inp, SCTP_FREE_SHOULD_USE_ABORT,
					SCTP_CALLED_AFTER_CMPSET_OFCLOSE);
		} else {
			flags = inp->sctp_flags;
			if ((flags &  SCTP_PCB_FLAGS_SOCKET_GONE) == 0) {
				goto try_again;
			} else {
				SCTP_INP_WUNLOCK(inp);
			}
		}
		return (error);
	}
#endif
#endif				/* IPSEC */
	SCTP_INP_WUNLOCK(inp);
	return (0);
}

#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
static int
sctp_bind(struct socket *so, struct sockaddr *addr, struct thread *p)
{
#elif defined(__FreeBSD__) || defined(__APPLE__)
static int
sctp_bind(struct socket *so, struct sockaddr *addr, struct proc *p) {
#elif defined(__Panda__) || defined(__Userspace__)
int
sctp_bind(struct socket *so, struct sockaddr *addr) {
	void *p = NULL;
#elif defined(__Windows__)
static int
sctp_bind(struct socket *so, struct sockaddr *addr, PKTHREAD p) {
#else
static int
sctp_bind(struct socket *so, struct mbuf *nam, struct proc *p)
{
	struct sockaddr *addr = nam ? mtod(nam, struct sockaddr *): NULL;

#endif
	struct sctp_inpcb *inp;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (EINVAL);
	}
	if (addr != NULL) {
#ifdef HAVE_SA_LEN
		if ((addr->sa_family != AF_INET) ||
		    (addr->sa_len != sizeof(struct sockaddr_in))) {
#else
		if (addr->sa_family != AF_INET) {
#endif
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			return (EINVAL);
		}
	}
	return (sctp_inpcb_bind(so, addr, NULL, p));
}

#endif
#if defined(__Userspace__)

int
sctpconn_attach(struct socket *so, int proto SCTP_UNUSED, uint32_t vrf_id)
{
	struct sctp_inpcb *inp;
	struct inpcb *ip_inp;
	int error;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp != NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (EINVAL);
	}
	if (so->so_snd.sb_hiwat == 0 || so->so_rcv.sb_hiwat == 0) {
		error = SCTP_SORESERVE(so, SCTP_BASE_SYSCTL(sctp_sendspace), SCTP_BASE_SYSCTL(sctp_recvspace));
		if (error) {
			return (error);
		}
	}
	error = sctp_inpcb_alloc(so, vrf_id);
	if (error) {
		return (error);
	}
	inp = (struct sctp_inpcb *)so->so_pcb;
	SCTP_INP_WLOCK(inp);
	inp->sctp_flags &= ~SCTP_PCB_FLAGS_BOUND_V6;
	inp->sctp_flags |= SCTP_PCB_FLAGS_BOUND_CONN;
	ip_inp = &inp->ip_inp.inp;
	ip_inp->inp_vflag |= INP_CONN;
	ip_inp->inp_ip_ttl = MODULE_GLOBAL(ip_defttl);
	SCTP_INP_WUNLOCK(inp);
	return (0);
}

int
sctpconn_bind(struct socket *so, struct sockaddr *addr)
{
	struct sctp_inpcb *inp;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (EINVAL);
	}
	if (addr != NULL) {
#ifdef HAVE_SA_LEN
		if ((addr->sa_family != AF_CONN) ||
		    (addr->sa_len != sizeof(struct sockaddr_conn))) {
#else
		if (addr->sa_family != AF_CONN) {
#endif
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			return (EINVAL);
		}
	}
	return (sctp_inpcb_bind(so, addr, NULL, NULL));
}

#endif
#if (defined(__FreeBSD__) && __FreeBSD_version > 690000) || defined(__Windows__) || defined(__Userspace__)
void
sctp_close(struct socket *so)
{
	struct sctp_inpcb *inp;
	uint32_t flags;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL)
		return;

	/* Inform all the lower layer assoc that we
	 * are done.
	 */
 sctp_must_try_again:
	flags = inp->sctp_flags;
#ifdef SCTP_LOG_CLOSING
	sctp_log_closing(inp, NULL, 17);
#endif
	if (((flags & SCTP_PCB_FLAGS_SOCKET_GONE) == 0) &&
	    (atomic_cmpset_int(&inp->sctp_flags, flags, (flags | SCTP_PCB_FLAGS_SOCKET_GONE | SCTP_PCB_FLAGS_CLOSE_IP)))) {
#if defined(__Userspace__)
		if (((so->so_options & SCTP_SO_LINGER) && (so->so_linger == 0)) ||
		    (so->so_rcv.sb_cc > 0)) {
#else
		if (((so->so_options & SO_LINGER) && (so->so_linger == 0)) ||
		    (so->so_rcv.sb_cc > 0)) {
#endif
#ifdef SCTP_LOG_CLOSING
			sctp_log_closing(inp, NULL, 13);
#endif
			sctp_inpcb_free(inp, SCTP_FREE_SHOULD_USE_ABORT,
					SCTP_CALLED_AFTER_CMPSET_OFCLOSE);
		} else {
#ifdef SCTP_LOG_CLOSING
			sctp_log_closing(inp, NULL, 14);
#endif
			sctp_inpcb_free(inp, SCTP_FREE_SHOULD_USE_GRACEFUL_CLOSE,
					SCTP_CALLED_AFTER_CMPSET_OFCLOSE);
		}
		/* The socket is now detached, no matter what
		 * the state of the SCTP association.
		 */
		SOCK_LOCK(so);
		SCTP_SB_CLEAR(so->so_snd);
		/* same for the rcv ones, they are only
		 * here for the accounting/select.
		 */
		SCTP_SB_CLEAR(so->so_rcv);

#if !defined(__APPLE__)
		/* Now null out the reference, we are completely detached. */
		so->so_pcb = NULL;
#endif
		SOCK_UNLOCK(so);
	} else {
		flags = inp->sctp_flags;
		if ((flags & SCTP_PCB_FLAGS_SOCKET_GONE) == 0) {
			goto sctp_must_try_again;
		}
	}
	return;
}

#else


int
sctp_detach(struct socket *so)
{
	struct sctp_inpcb *inp;
	uint32_t flags;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
#if defined(__FreeBSD__) && __FreeBSD_version > 690000
		return;
#else
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (EINVAL);
#endif
	}
 sctp_must_try_again:
	flags = inp->sctp_flags;
#ifdef SCTP_LOG_CLOSING
	sctp_log_closing(inp, NULL, 17);
#endif
	if (((flags & SCTP_PCB_FLAGS_SOCKET_GONE) == 0) &&
	    (atomic_cmpset_int(&inp->sctp_flags, flags, (flags | SCTP_PCB_FLAGS_SOCKET_GONE | SCTP_PCB_FLAGS_CLOSE_IP)))) {
#if defined(__Userspace__)
		if (((so->so_options & SCTP_SO_LINGER) && (so->so_linger == 0)) ||
		    (so->so_rcv.sb_cc > 0)) {
#else
		if (((so->so_options & SO_LINGER) && (so->so_linger == 0)) ||
		    (so->so_rcv.sb_cc > 0)) {
#endif
#ifdef SCTP_LOG_CLOSING
			sctp_log_closing(inp, NULL, 13);
#endif
			sctp_inpcb_free(inp, SCTP_FREE_SHOULD_USE_ABORT,
					SCTP_CALLED_AFTER_CMPSET_OFCLOSE);
		} else {
#ifdef SCTP_LOG_CLOSING
			sctp_log_closing(inp, NULL, 13);
#endif
			sctp_inpcb_free(inp, SCTP_FREE_SHOULD_USE_GRACEFUL_CLOSE,
					SCTP_CALLED_AFTER_CMPSET_OFCLOSE);
		}
		/* The socket is now detached, no matter what
		 * the state of the SCTP association.
		 */
		SCTP_SB_CLEAR(so->so_snd);
		/* same for the rcv ones, they are only
		 * here for the accounting/select.
		 */
		SCTP_SB_CLEAR(so->so_rcv);
#if !defined(__APPLE__)
		/* Now disconnect */
		so->so_pcb = NULL;
#endif
	} else {
		flags = inp->sctp_flags;
		if ((flags & SCTP_PCB_FLAGS_SOCKET_GONE) == 0) {
			goto sctp_must_try_again;
		}
	}
#if defined(__FreeBSD__) && __FreeBSD_version > 690000
	return;
#else
	return (0);
#endif
}
#endif

#if defined(__Userspace__)
/* __Userspace__ is not calling sctp_sendm */
#endif
#if !(defined(__Panda__) || defined(__Windows__))
int
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
sctp_sendm(struct socket *so, int flags, struct mbuf *m, struct sockaddr *addr,
    struct mbuf *control, struct thread *p);

#else
sctp_sendm(struct socket *so, int flags, struct mbuf *m, struct sockaddr *addr,
    struct mbuf *control, struct proc *p);

#endif

int
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
sctp_sendm(struct socket *so, int flags, struct mbuf *m, struct sockaddr *addr,
    struct mbuf *control, struct thread *p)
{
#else
sctp_sendm(struct socket *so, int flags, struct mbuf *m, struct sockaddr *addr,
    struct mbuf *control, struct proc *p)
{
#endif
	struct sctp_inpcb *inp;
	int error;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		if (control) {
			sctp_m_freem(control);
			control = NULL;
		}
		SCTP_LTRACE_ERR_RET_PKT(m, inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		sctp_m_freem(m);
		return (EINVAL);
	}
	/* Got to have an to address if we are NOT a connected socket */
	if ((addr == NULL) &&
	    ((inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE))) {
		goto connected_type;
	} else if (addr == NULL) {
		SCTP_LTRACE_ERR_RET_PKT(m, inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EDESTADDRREQ);
		error = EDESTADDRREQ;
		sctp_m_freem(m);
		if (control) {
			sctp_m_freem(control);
			control = NULL;
		}
		return (error);
	}
#ifdef INET6
	if (addr->sa_family != AF_INET) {
		/* must be a v4 address! */
		SCTP_LTRACE_ERR_RET_PKT(m, inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EDESTADDRREQ);
		sctp_m_freem(m);
		if (control) {
			sctp_m_freem(control);
			control = NULL;
		}
		error = EDESTADDRREQ;
		return (error);
	}
#endif				/* INET6 */
connected_type:
	/* now what about control */
	if (control) {
		if (inp->control) {
			SCTP_PRINTF("huh? control set?\n");
			sctp_m_freem(inp->control);
			inp->control = NULL;
		}
		inp->control = control;
	}
	/* Place the data */
	if (inp->pkt) {
		SCTP_BUF_NEXT(inp->pkt_last) = m;
		inp->pkt_last = m;
	} else {
		inp->pkt_last = inp->pkt = m;
	}
	if (
#if defined(__FreeBSD__) || defined(__APPLE__)
	/* FreeBSD uses a flag passed */
	    ((flags & PRUS_MORETOCOME) == 0)
#else
	    1			/* Open BSD does not have any "more to come"
				 * indication */
#endif
	    ) {
		/*
		 * note with the current version this code will only be used
		 * by OpenBSD-- NetBSD, FreeBSD, and MacOS have methods for
		 * re-defining sosend to use the sctp_sosend. One can
		 * optionally switch back to this code (by changing back the
		 * definitions) but this is not advisable. This code is used
		 * by FreeBSD when sending a file with sendfile() though.
		 */
		int ret;

		ret = sctp_output(inp, inp->pkt, addr, inp->control, p, flags);
		inp->pkt = NULL;
		inp->control = NULL;
		return (ret);
	} else {
		return (0);
	}
}
#endif

int
sctp_disconnect(struct socket *so)
{
	struct sctp_inpcb *inp;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOTCONN);
		return (ENOTCONN);
	}
	SCTP_INP_RLOCK(inp);
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) {
		if (LIST_EMPTY(&inp->sctp_asoc_list)) {
			/* No connection */
			SCTP_INP_RUNLOCK(inp);
			return (0);
		} else {
			struct sctp_association *asoc;
			struct sctp_tcb *stcb;

			stcb = LIST_FIRST(&inp->sctp_asoc_list);
			if (stcb == NULL) {
				SCTP_INP_RUNLOCK(inp);
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				return (EINVAL);
			}
			SCTP_TCB_LOCK(stcb);
			asoc = &stcb->asoc;
			if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
				/* We are about to be freed, out of here */
				SCTP_TCB_UNLOCK(stcb);
				SCTP_INP_RUNLOCK(inp);
				return (0);
			}
#if defined(__Userspace__)
			if (((so->so_options & SCTP_SO_LINGER) &&
			     (so->so_linger == 0)) ||
			    (so->so_rcv.sb_cc > 0)) {
#else
			if (((so->so_options & SO_LINGER) &&
			     (so->so_linger == 0)) ||
			    (so->so_rcv.sb_cc > 0)) {
#endif
				if (SCTP_GET_STATE(asoc) !=
				    SCTP_STATE_COOKIE_WAIT) {
					/* Left with Data unread */
					struct mbuf *err;

					err = sctp_get_mbuf_for_msg(sizeof(struct sctp_paramhdr), 0, M_NOWAIT, 1, MT_DATA);
					if (err) {
						/*
						 * Fill in the user
						 * initiated abort
						 */
						struct sctp_paramhdr *ph;

						ph = mtod(err, struct sctp_paramhdr *);
						SCTP_BUF_LEN(err) = sizeof(struct sctp_paramhdr);
						ph->param_type = htons(SCTP_CAUSE_USER_INITIATED_ABT);
						ph->param_length = htons(SCTP_BUF_LEN(err));
					}
					sctp_send_abort_tcb(stcb, err, SCTP_SO_LOCKED);
					SCTP_STAT_INCR_COUNTER32(sctps_aborted);
				}
				SCTP_INP_RUNLOCK(inp);
				if ((SCTP_GET_STATE(&stcb->asoc) == SCTP_STATE_OPEN) ||
				    (SCTP_GET_STATE(&stcb->asoc) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
					SCTP_STAT_DECR_GAUGE32(sctps_currestab);
				}
				(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC, SCTP_FROM_SCTP_USRREQ+SCTP_LOC_3);
				/* No unlock tcb assoc is gone */
				return (0);
			}
			if (TAILQ_EMPTY(&asoc->send_queue) &&
			    TAILQ_EMPTY(&asoc->sent_queue) &&
			    (asoc->stream_queue_cnt == 0)) {
				/* there is nothing queued to send, so done */
				if (asoc->locked_on_sending) {
					goto abort_anyway;
				}
				if ((SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_SENT) &&
				    (SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_ACK_SENT)) {
					/* only send SHUTDOWN 1st time thru */
					struct sctp_nets *netp;

					if ((SCTP_GET_STATE(asoc) == SCTP_STATE_OPEN) ||
					    (SCTP_GET_STATE(asoc) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
						SCTP_STAT_DECR_GAUGE32(sctps_currestab);
					}
					SCTP_SET_STATE(asoc, SCTP_STATE_SHUTDOWN_SENT);
					SCTP_CLEAR_SUBSTATE(asoc, SCTP_STATE_SHUTDOWN_PENDING);
					sctp_stop_timers_for_shutdown(stcb);
					if (stcb->asoc.alternate) {
						netp = stcb->asoc.alternate;
					} else {
						netp = stcb->asoc.primary_destination;
					}
					sctp_send_shutdown(stcb,netp);
					sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWN,
							 stcb->sctp_ep, stcb, netp);
					sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD,
							 stcb->sctp_ep, stcb, netp);
					sctp_chunk_output(stcb->sctp_ep, stcb, SCTP_OUTPUT_FROM_T3, SCTP_SO_LOCKED);
				}
			} else {
				/*
				 * we still got (or just got) data to send,
				 * so set SHUTDOWN_PENDING
				 */
				/*
				 * XXX sockets draft says that SCTP_EOF
				 * should be sent with no data. currently,
				 * we will allow user data to be sent first
				 * and move to SHUTDOWN-PENDING
				 */
				struct sctp_nets *netp;
				if (stcb->asoc.alternate) {
					netp = stcb->asoc.alternate;
				} else {
					netp = stcb->asoc.primary_destination;
				}

				asoc->state |= SCTP_STATE_SHUTDOWN_PENDING;
				sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD, stcb->sctp_ep, stcb,
						 netp);
				if (asoc->locked_on_sending) {
					/* Locked to send out the data */
					struct sctp_stream_queue_pending *sp;
					sp = TAILQ_LAST(&asoc->locked_on_sending->outqueue, sctp_streamhead);
					if (sp == NULL) {
						SCTP_PRINTF("Error, sp is NULL, locked on sending is non-null strm:%d\n",
							    asoc->locked_on_sending->stream_no);
					} else {
						if ((sp->length == 0) && (sp->msg_is_complete == 0))
							asoc->state |= SCTP_STATE_PARTIAL_MSG_LEFT;
					}
				}
				if (TAILQ_EMPTY(&asoc->send_queue) &&
				    TAILQ_EMPTY(&asoc->sent_queue) &&
				    (asoc->state & SCTP_STATE_PARTIAL_MSG_LEFT)) {
					struct mbuf *op_err;
				abort_anyway:
					op_err = sctp_get_mbuf_for_msg(sizeof(struct sctp_paramhdr),
								       0, M_NOWAIT, 1, MT_DATA);
					if (op_err) {
						/* Fill in the user initiated abort */
						struct sctp_paramhdr *ph;

						SCTP_BUF_LEN(op_err) = sizeof(struct sctp_paramhdr);
						ph = mtod(op_err, struct sctp_paramhdr *);
						ph->param_type = htons(SCTP_CAUSE_USER_INITIATED_ABT);
						ph->param_length = htons(SCTP_BUF_LEN(op_err));
					}
					stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_USRREQ+SCTP_LOC_4;
					sctp_send_abort_tcb(stcb, op_err, SCTP_SO_LOCKED);
					SCTP_STAT_INCR_COUNTER32(sctps_aborted);
					if ((SCTP_GET_STATE(&stcb->asoc) == SCTP_STATE_OPEN) ||
					    (SCTP_GET_STATE(&stcb->asoc) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
						SCTP_STAT_DECR_GAUGE32(sctps_currestab);
					}
					SCTP_INP_RUNLOCK(inp);
					(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC, SCTP_FROM_SCTP_USRREQ+SCTP_LOC_5);
					return (0);
				} else {
					sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_CLOSING, SCTP_SO_LOCKED);
				}
			}
			soisdisconnecting(so);
			SCTP_TCB_UNLOCK(stcb);
			SCTP_INP_RUNLOCK(inp);
			return (0);
		}
		/* not reached */
	} else {
		/* UDP model does not support this */
		SCTP_INP_RUNLOCK(inp);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EOPNOTSUPP);
		return (EOPNOTSUPP);
	}
}

#if defined(__FreeBSD__) || defined(__Windows__) || defined(__Userspace__)
int
sctp_flush(struct socket *so, int how)
{
        /*
	 * We will just clear out the values and let
	 * subsequent close clear out the data, if any.
	 * Note if the user did a shutdown(SHUT_RD) they
	 * will not be able to read the data, the socket
	 * will block that from happening.
	 */
	struct sctp_inpcb *inp;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (EINVAL);
	}
	SCTP_INP_RLOCK(inp);
	/* For the 1 to many model this does nothing */
	if (inp->sctp_flags & SCTP_PCB_FLAGS_UDPTYPE) {
		SCTP_INP_RUNLOCK(inp);
		return (0);
	}
	SCTP_INP_RUNLOCK(inp);
        if ((how == PRU_FLUSH_RD) || (how == PRU_FLUSH_RDWR)) {
		/* First make sure the sb will be happy, we don't
		 * use these except maybe the count
		 */
		SCTP_INP_WLOCK(inp);
		SCTP_INP_READ_LOCK(inp);
		inp->sctp_flags |= SCTP_PCB_FLAGS_SOCKET_CANT_READ;
		SCTP_INP_READ_UNLOCK(inp);
		SCTP_INP_WUNLOCK(inp);
		so->so_rcv.sb_cc = 0;
		so->so_rcv.sb_mbcnt = 0;
		so->so_rcv.sb_mb = NULL;
	}
        if ((how == PRU_FLUSH_WR) || (how == PRU_FLUSH_RDWR)) {
		/* First make sure the sb will be happy, we don't
		 * use these except maybe the count
		 */
		so->so_snd.sb_cc = 0;
		so->so_snd.sb_mbcnt = 0;
		so->so_snd.sb_mb = NULL;

	}
	return (0);
}
#endif

int
sctp_shutdown(struct socket *so)
{
	struct sctp_inpcb *inp;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (EINVAL);
	}
	SCTP_INP_RLOCK(inp);
	/* For UDP model this is a invalid call */
	if (!((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
	      (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL))) {
		/* Restore the flags that the soshutdown took away. */
#if (defined(__FreeBSD__) && __FreeBSD_version >= 502115) || defined(__Windows__)
		SOCKBUF_LOCK(&so->so_rcv);
		so->so_rcv.sb_state &= ~SBS_CANTRCVMORE;
		SOCKBUF_UNLOCK(&so->so_rcv);
#else
		so->so_state &= ~SS_CANTRCVMORE;
#endif
		/* This proc will wakeup for read and do nothing (I hope) */
		SCTP_INP_RUNLOCK(inp);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EOPNOTSUPP);
		return (EOPNOTSUPP);
	}
	/*
	 * Ok if we reach here its the TCP model and it is either a SHUT_WR
	 * or SHUT_RDWR. This means we put the shutdown flag against it.
	 */
	{
		struct sctp_tcb *stcb;
		struct sctp_association *asoc;

		if ((so->so_state &
		     (SS_ISCONNECTED|SS_ISCONNECTING|SS_ISDISCONNECTING)) == 0) {
			SCTP_INP_RUNLOCK(inp);
			return (ENOTCONN);
		}
		socantsendmore(so);

		stcb = LIST_FIRST(&inp->sctp_asoc_list);
		if (stcb == NULL) {
			/*
			 * Ok we hit the case that the shutdown call was
			 * made after an abort or something. Nothing to do
			 * now.
			 */
			SCTP_INP_RUNLOCK(inp);
			return (0);
		}
		SCTP_TCB_LOCK(stcb);
		asoc = &stcb->asoc;
		if (TAILQ_EMPTY(&asoc->send_queue) &&
		    TAILQ_EMPTY(&asoc->sent_queue) &&
		    (asoc->stream_queue_cnt == 0)) {
			if (asoc->locked_on_sending) {
				goto abort_anyway;
			}
			/* there is nothing queued to send, so I'm done... */
			if (SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_SENT) {
				/* only send SHUTDOWN the first time through */
				struct sctp_nets *netp;

				if ((SCTP_GET_STATE(asoc) == SCTP_STATE_OPEN) ||
				    (SCTP_GET_STATE(asoc) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
					SCTP_STAT_DECR_GAUGE32(sctps_currestab);
				}
				SCTP_SET_STATE(asoc, SCTP_STATE_SHUTDOWN_SENT);
				SCTP_CLEAR_SUBSTATE(asoc, SCTP_STATE_SHUTDOWN_PENDING);
				sctp_stop_timers_for_shutdown(stcb);
				if (stcb->asoc.alternate) {
					netp = stcb->asoc.alternate;
				} else {
					netp = stcb->asoc.primary_destination;
				}
				sctp_send_shutdown(stcb, netp);
				sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWN,
						 stcb->sctp_ep, stcb, netp);
				sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD,
						 stcb->sctp_ep, stcb, netp);
				sctp_chunk_output(stcb->sctp_ep, stcb, SCTP_OUTPUT_FROM_T3, SCTP_SO_LOCKED);
			}
		} else {
			/*
			 * we still got (or just got) data to send, so set
			 * SHUTDOWN_PENDING
			 */
			struct sctp_nets *netp;
			if (stcb->asoc.alternate) {
				netp = stcb->asoc.alternate;
			} else {
				netp = stcb->asoc.primary_destination;
			}

			asoc->state |= SCTP_STATE_SHUTDOWN_PENDING;
			sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD, stcb->sctp_ep, stcb,
					 netp);

			if (asoc->locked_on_sending) {
				/* Locked to send out the data */
				struct sctp_stream_queue_pending *sp;
				sp = TAILQ_LAST(&asoc->locked_on_sending->outqueue, sctp_streamhead);
				if (sp == NULL) {
					SCTP_PRINTF("Error, sp is NULL, locked on sending is non-null strm:%d\n",
						    asoc->locked_on_sending->stream_no);
				} else {
					if ((sp->length == 0)  && (sp-> msg_is_complete == 0)) {
						asoc->state |= SCTP_STATE_PARTIAL_MSG_LEFT;
					}
				}
			}
			if (TAILQ_EMPTY(&asoc->send_queue) &&
			    TAILQ_EMPTY(&asoc->sent_queue) &&
			    (asoc->state & SCTP_STATE_PARTIAL_MSG_LEFT)) {
				struct mbuf *op_err;
			abort_anyway:
				op_err = sctp_get_mbuf_for_msg(sizeof(struct sctp_paramhdr),
							       0, M_NOWAIT, 1, MT_DATA);
				if (op_err) {
					/* Fill in the user initiated abort */
					struct sctp_paramhdr *ph;

					SCTP_BUF_LEN(op_err) = sizeof(struct sctp_paramhdr);
					ph = mtod(op_err, struct sctp_paramhdr *);
					ph->param_type = htons( SCTP_CAUSE_USER_INITIATED_ABT);
					ph->param_length = htons(SCTP_BUF_LEN(op_err));
				}
				stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_USRREQ+SCTP_LOC_6;
				sctp_abort_an_association(stcb->sctp_ep, stcb,
							  op_err, SCTP_SO_LOCKED);
				goto skip_unlock;
			} else {
				sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_CLOSING, SCTP_SO_LOCKED);
			}
		}
		SCTP_TCB_UNLOCK(stcb);
	}
 skip_unlock:
	SCTP_INP_RUNLOCK(inp);
	return (0);
}

/*
 * copies a "user" presentable address and removes embedded scope, etc.
 * returns 0 on success, 1 on error
 */
static uint32_t
sctp_fill_user_address(struct sockaddr_storage *ss, struct sockaddr *sa)
{
#ifdef INET6
#if defined(SCTP_EMBEDDED_V6_SCOPE)
	struct sockaddr_in6 lsa6;

	sa = (struct sockaddr *)sctp_recover_scope((struct sockaddr_in6 *)sa,
	    &lsa6);
#endif
#endif
#ifdef HAVE_SA_LEN
	memcpy(ss, sa, sa->sa_len);
#else
	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		memcpy(ss, sa, sizeof(struct sockaddr_in));
		break;
#endif
#ifdef INET6
	case AF_INET6:
		memcpy(ss, sa, sizeof(struct sockaddr_in6));
		break;
#endif
#if defined(__Userspace__)
	case AF_CONN:
		memcpy(ss, sa, sizeof(struct sockaddr_conn));
		break;
#endif
	default:
		/* TSNH */
		break;
	}
#endif
	return (0);
}



/*
 * NOTE: assumes addr lock is held
 */
static size_t
sctp_fill_up_addresses_vrf(struct sctp_inpcb *inp,
			   struct sctp_tcb *stcb,
			   size_t limit,
			   struct sockaddr_storage *sas,
			   uint32_t vrf_id)
{
	struct sctp_ifn *sctp_ifn;
	struct sctp_ifa *sctp_ifa;
	size_t actual;
	int loopback_scope;
#if defined(INET)
	int ipv4_local_scope, ipv4_addr_legal;
#endif
#if defined(INET6)
	int local_scope, site_scope, ipv6_addr_legal;
#endif
#if defined(__Userspace__)
	int conn_addr_legal;
#endif
	struct sctp_vrf *vrf;

	actual = 0;
	if (limit <= 0)
		return (actual);

	if (stcb) {
		/* Turn on all the appropriate scope */
		loopback_scope = stcb->asoc.scope.loopback_scope;
#if defined(INET)
		ipv4_local_scope = stcb->asoc.scope.ipv4_local_scope;
		ipv4_addr_legal = stcb->asoc.scope.ipv4_addr_legal;
#endif
#if defined(INET6)
		local_scope = stcb->asoc.scope.local_scope;
		site_scope = stcb->asoc.scope.site_scope;
		ipv6_addr_legal = stcb->asoc.scope.ipv6_addr_legal;
#endif
#if defined(__Userspace__)
		conn_addr_legal = stcb->asoc.scope.conn_addr_legal;
#endif
	} else {
		/* Use generic values for endpoints. */
		loopback_scope = 1;
#if defined(INET)
		ipv4_local_scope = 1;
#endif
#if defined(INET6)
		local_scope = 1;
		site_scope = 1;
#endif
		if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
#if defined(INET6)
			ipv6_addr_legal = 1;
#endif
#if defined(INET)
			if (SCTP_IPV6_V6ONLY(inp)) {
				ipv4_addr_legal = 0;
			} else {
				ipv4_addr_legal = 1;
			}
#endif
#if defined(__Userspace__)
			conn_addr_legal = 0;
#endif
		} else {
#if defined(INET6)
			ipv6_addr_legal = 0;
#endif
#if defined(__Userspace__)
			if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_CONN) {
				conn_addr_legal = 1;
#if defined(INET)
				ipv4_addr_legal = 0;
#endif
			} else {
				conn_addr_legal = 0;
#if defined(INET)
				ipv4_addr_legal = 1;
#endif
			}
#else
#if defined(INET)
			ipv4_addr_legal = 1;
#endif
#endif
		}
	}
	vrf = sctp_find_vrf(vrf_id);
	if (vrf == NULL) {
		return (0);
	}
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		LIST_FOREACH(sctp_ifn, &vrf->ifnlist, next_ifn) {
			if ((loopback_scope == 0) &&
			    SCTP_IFN_IS_IFT_LOOP(sctp_ifn)) {
				/* Skip loopback if loopback_scope not set */
				continue;
			}
			LIST_FOREACH(sctp_ifa, &sctp_ifn->ifalist, next_ifa) {
				if (stcb) {
					/*
					 * For the BOUND-ALL case, the list
					 * associated with a TCB is Always
					 * considered a reverse list.. i.e.
					 * it lists addresses that are NOT
					 * part of the association. If this
					 * is one of those we must skip it.
					 */
					if (sctp_is_addr_restricted(stcb,
								    sctp_ifa)) {
						continue;
					}
				}
				switch (sctp_ifa->address.sa.sa_family) {
#ifdef INET
				case AF_INET:
					if (ipv4_addr_legal) {
						struct sockaddr_in *sin;

						sin = (struct sockaddr_in *)&sctp_ifa->address.sa;
						if (sin->sin_addr.s_addr == 0) {
							/*
							 * we skip unspecifed
							 * addresses
							 */
							continue;
						}
						if ((ipv4_local_scope == 0) &&
						    (IN4_ISPRIVATE_ADDRESS(&sin->sin_addr))) {
							continue;
						}
#ifdef INET6
						if (sctp_is_feature_on(inp,SCTP_PCB_FLAGS_NEEDS_MAPPED_V4)) {
							in6_sin_2_v4mapsin6(sin, (struct sockaddr_in6 *)sas);
							((struct sockaddr_in6 *)sas)->sin6_port = inp->sctp_lport;
							sas = (struct sockaddr_storage *)((caddr_t)sas + sizeof(struct sockaddr_in6));
							actual += sizeof(struct sockaddr_in6);
						} else {
#endif
							memcpy(sas, sin, sizeof(*sin));
							((struct sockaddr_in *)sas)->sin_port = inp->sctp_lport;
							sas = (struct sockaddr_storage *)((caddr_t)sas + sizeof(*sin));
							actual += sizeof(*sin);
#ifdef INET6
						}
#endif
						if (actual >= limit) {
							return (actual);
						}
					} else {
						continue;
					}
					break;
#endif
#ifdef INET6
				case AF_INET6:
					if (ipv6_addr_legal) {
						struct sockaddr_in6 *sin6;

#if defined(SCTP_EMBEDDED_V6_SCOPE) && !defined(SCTP_KAME)
						struct sockaddr_in6 lsa6;
#endif
						sin6 = (struct sockaddr_in6 *)&sctp_ifa->address.sa;
						if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
							/*
							 * we skip unspecifed
							 * addresses
							 */
							continue;
						}
						if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
							if (local_scope == 0)
								continue;
#if defined(SCTP_EMBEDDED_V6_SCOPE)
							if (sin6->sin6_scope_id == 0) {
#ifdef SCTP_KAME
								if (sa6_recoverscope(sin6) != 0)
									/*
									 * bad link
									 * local
									 * address
									 */
									continue;
#else
								lsa6 = *sin6;
								if (in6_recoverscope(&lsa6,
										     &lsa6.sin6_addr,
										     NULL))
									/*
									 * bad link
									 * local
									 * address
									 */
								continue;
								sin6 = &lsa6;
#endif				/* SCTP_KAME */
							}
#endif /* SCTP_EMBEDDED_V6_SCOPE */
						}
						if ((site_scope == 0) &&
						    (IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr))) {
							continue;
						}
						memcpy(sas, sin6, sizeof(*sin6));
						((struct sockaddr_in6 *)sas)->sin6_port = inp->sctp_lport;
						sas = (struct sockaddr_storage *)((caddr_t)sas + sizeof(*sin6));
						actual += sizeof(*sin6);
						if (actual >= limit) {
							return (actual);
						}
					} else {
						continue;
					}
					break;
#endif
#if defined(__Userspace__)
				case AF_CONN:
					if (conn_addr_legal) {
						memcpy(sas, &sctp_ifa->address.sconn, sizeof(struct sockaddr_conn));
						((struct sockaddr_conn *)sas)->sconn_port = inp->sctp_lport;
						sas = (struct sockaddr_storage *)((caddr_t)sas + sizeof(struct sockaddr_conn));
						actual += sizeof(struct sockaddr_conn);
						if (actual >= limit) {
							return (actual);
						}
					} else {
						continue;
					}
#endif
				default:
					/* TSNH */
					break;
				}
			}
		}
	} else {
		struct sctp_laddr *laddr;
#ifndef HAVE_SA_LEN
		uint32_t sa_len = 0;
#endif

		LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
			if (stcb) {
				if (sctp_is_addr_restricted(stcb, laddr->ifa)) {
					continue;
				}
			}
			if (sctp_fill_user_address(sas, &laddr->ifa->address.sa))
				continue;
			switch (laddr->ifa->address.sa.sa_family) {
#ifdef INET
			case AF_INET:
				((struct sockaddr_in *)sas)->sin_port = inp->sctp_lport;
				break;
#endif
#ifdef INET6
			case AF_INET6:
				((struct sockaddr_in6 *)sas)->sin6_port = inp->sctp_lport;
				break;
#endif
#if defined(__Userspace__)
			case AF_CONN:
				((struct sockaddr_conn *)sas)->sconn_port = inp->sctp_lport;
				break;
#endif
			default:
				/* TSNH */
				break;
			}
#ifdef HAVE_SA_LEN
			sas = (struct sockaddr_storage *)((caddr_t)sas +
							  laddr->ifa->address.sa.sa_len);
			actual += laddr->ifa->address.sa.sa_len;
#else
			switch (laddr->ifa->address.sa.sa_family) {
#ifdef INET
			case AF_INET:
				sa_len = sizeof(struct sockaddr_in);
				break;
#endif
#ifdef INET6
			case AF_INET6:
				sa_len = sizeof(struct sockaddr_in6);
				break;
#endif
#if defined(__Userspace__)
			case AF_CONN:
				sa_len = sizeof(struct sockaddr_conn);
				break;
#endif
			default:
				/* TSNH */
				break;
			}
			sas = (struct sockaddr_storage *)((caddr_t)sas + sa_len);
			actual += sa_len;
#endif
			if (actual >= limit) {
				return (actual);
			}
		}
	}
	return (actual);
}

static size_t
sctp_fill_up_addresses(struct sctp_inpcb *inp,
                       struct sctp_tcb *stcb,
                       size_t limit,
                       struct sockaddr_storage *sas)
{
	size_t size = 0;
#ifdef SCTP_MVRF
	uint32_t id;
#endif

	SCTP_IPI_ADDR_RLOCK();
#ifdef SCTP_MVRF
/*
 * FIX ME: ?? this WILL report duplicate addresses if they appear
 * in more than one VRF.
 */
	/* fill up addresses for all VRFs on the endpoint */
	for (id = 0; (id < inp->num_vrfs) && (size < limit); id++) {
		size += sctp_fill_up_addresses_vrf(inp, stcb, limit, sas,
						   inp->m_vrf_ids[id]);
		sas = (struct sockaddr_storage *)((caddr_t)sas + size);
	}
#else
	/* fill up addresses for the endpoint's default vrf */
	size = sctp_fill_up_addresses_vrf(inp, stcb, limit, sas,
					  inp->def_vrf_id);
#endif
	SCTP_IPI_ADDR_RUNLOCK();
	return (size);
}

/*
 * NOTE: assumes addr lock is held
 */
static int
sctp_count_max_addresses_vrf(struct sctp_inpcb *inp, uint32_t vrf_id)
{
	int cnt = 0;
	struct sctp_vrf *vrf = NULL;

	/*
	 * In both sub-set bound an bound_all cases we return the MAXIMUM
	 * number of addresses that you COULD get. In reality the sub-set
	 * bound may have an exclusion list for a given TCB OR in the
	 * bound-all case a TCB may NOT include the loopback or other
	 * addresses as well.
	 */
	vrf = sctp_find_vrf(vrf_id);
	if (vrf == NULL) {
		return (0);
	}
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		struct sctp_ifn *sctp_ifn;
		struct sctp_ifa *sctp_ifa;

		LIST_FOREACH(sctp_ifn, &vrf->ifnlist, next_ifn) {
			LIST_FOREACH(sctp_ifa, &sctp_ifn->ifalist, next_ifa) {
				/* Count them if they are the right type */
				switch (sctp_ifa->address.sa.sa_family) {
#ifdef INET
				case AF_INET:
					if (sctp_is_feature_on(inp,SCTP_PCB_FLAGS_NEEDS_MAPPED_V4))
						cnt += sizeof(struct sockaddr_in6);
					else
						cnt += sizeof(struct sockaddr_in);
					break;
#endif
#ifdef INET6
				case AF_INET6:
					cnt += sizeof(struct sockaddr_in6);
					break;
#endif
#if defined(__Userspace__)
				case AF_CONN:
					cnt += sizeof(struct sockaddr_conn);
					break;
#endif
				default:
					break;
				}
			}
		}
	} else {
		struct sctp_laddr *laddr;

		LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
			switch (laddr->ifa->address.sa.sa_family) {
#ifdef INET
			case AF_INET:
				if (sctp_is_feature_on(inp,SCTP_PCB_FLAGS_NEEDS_MAPPED_V4))
					cnt += sizeof(struct sockaddr_in6);
				else
					cnt += sizeof(struct sockaddr_in);
				break;
#endif
#ifdef INET6
			case AF_INET6:
				cnt += sizeof(struct sockaddr_in6);
				break;
#endif
#if defined(__Userspace__)
			case AF_CONN:
				cnt += sizeof(struct sockaddr_conn);
				break;
#endif
			default:
				break;
			}
		}
	}
	return (cnt);
}

static int
sctp_count_max_addresses(struct sctp_inpcb *inp)
{
	int cnt = 0;
#ifdef SCTP_MVRF
	int id;
#endif

	SCTP_IPI_ADDR_RLOCK();
#ifdef SCTP_MVRF
/*
 * FIX ME: ?? this WILL count duplicate addresses if they appear
 * in more than one VRF.
 */
	/* count addresses for all VRFs on the endpoint */
	for (id = 0; id < inp->num_vrfs; id++) {
		cnt += sctp_count_max_addresses_vrf(inp, inp->m_vrf_ids[id]);
	}
#else
	/* count addresses for the endpoint's default VRF */
	cnt = sctp_count_max_addresses_vrf(inp, inp->def_vrf_id);
#endif
	SCTP_IPI_ADDR_RUNLOCK();
	return (cnt);
}

static int
sctp_do_connect_x(struct socket *so, struct sctp_inpcb *inp, void *optval,
		  size_t optsize, void *p, int delay)
{
	int error = 0;
	int creat_lock_on = 0;
	struct sctp_tcb *stcb = NULL;
	struct sockaddr *sa;
	int num_v6 = 0, num_v4 = 0, *totaddrp, totaddr;
	uint32_t vrf_id;
	int bad_addresses = 0;
	sctp_assoc_t *a_id;

	SCTPDBG(SCTP_DEBUG_PCB1, "Connectx called\n");

	if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) &&
	    (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED)) {
		/* We are already connected AND the TCP model */
		SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_USRREQ, EADDRINUSE);
		return (EADDRINUSE);
	}

	if ((inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) &&
	    (sctp_is_feature_off(inp, SCTP_PCB_FLAGS_PORTREUSE))) {
		SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (EINVAL);
	}

	if (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) {
		SCTP_INP_RLOCK(inp);
		stcb = LIST_FIRST(&inp->sctp_asoc_list);
		SCTP_INP_RUNLOCK(inp);
	}
	if (stcb) {
		SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_USRREQ, EALREADY);
		return (EALREADY);
	}
	SCTP_INP_INCR_REF(inp);
	SCTP_ASOC_CREATE_LOCK(inp);
	creat_lock_on = 1;
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE)) {
		SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_USRREQ, EFAULT);
		error = EFAULT;
		goto out_now;
	}
	totaddrp = (int *)optval;
	totaddr = *totaddrp;
	sa = (struct sockaddr *)(totaddrp + 1);
	stcb = sctp_connectx_helper_find(inp, sa, &totaddr, &num_v4, &num_v6, &error, (optsize - sizeof(int)), &bad_addresses);
	if ((stcb != NULL) || bad_addresses) {
		/* Already have or am bring up an association */
		SCTP_ASOC_CREATE_UNLOCK(inp);
		creat_lock_on = 0;
		if (stcb)
			SCTP_TCB_UNLOCK(stcb);
		if (bad_addresses == 0) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EALREADY);
			error = EALREADY;
		}
		goto out_now;
	}
#ifdef INET6
	if (((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) == 0) &&
	    (num_v6 > 0)) {
		error = EINVAL;
		goto out_now;
	}
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
	    (num_v4 > 0)) {
		struct in6pcb *inp6;

		inp6 = (struct in6pcb *)inp;
		if (SCTP_IPV6_V6ONLY(inp6)) {
			/*
			 * if IPV6_V6ONLY flag, ignore connections destined
			 * to a v4 addr or v4-mapped addr
			 */
			SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			goto out_now;
		}
	}
#endif				/* INET6 */
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND) ==
	    SCTP_PCB_FLAGS_UNBOUND) {
		/* Bind a ephemeral port */
		error = sctp_inpcb_bind(so, NULL, NULL, p);
		if (error) {
			goto out_now;
		}
	}

	/* FIX ME: do we want to pass in a vrf on the connect call? */
	vrf_id = inp->def_vrf_id;


	/* We are GOOD to go */
	stcb = sctp_aloc_assoc(inp, sa, &error, 0, vrf_id,
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
			       (struct thread *)p
#elif defined(__Windows__)
			       (PKTHREAD)p
#else
			       (struct proc *)p
#endif
		);
	if (stcb == NULL) {
		/* Gak! no memory */
		goto out_now;
	}
	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) {
		stcb->sctp_ep->sctp_flags |= SCTP_PCB_FLAGS_CONNECTED;
		/* Set the connected flag so we can queue data */
		soisconnecting(so);
	}
	SCTP_SET_STATE(&stcb->asoc, SCTP_STATE_COOKIE_WAIT);
	/* move to second address */
	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		sa = (struct sockaddr *)((caddr_t)sa + sizeof(struct sockaddr_in));
		break;
#endif
#ifdef INET6
	case AF_INET6:
		sa = (struct sockaddr *)((caddr_t)sa + sizeof(struct sockaddr_in6));
		break;
#endif
	default:
		break;
	}

	error = 0;
	sctp_connectx_helper_add(stcb, sa, (totaddr-1), &error);
	/* Fill in the return id */
	if (error) {
		(void)sctp_free_assoc(inp, stcb, SCTP_PCBFREE_FORCE, SCTP_FROM_SCTP_USRREQ+SCTP_LOC_6);
		goto out_now;
	}
	a_id = (sctp_assoc_t *)optval;
	*a_id = sctp_get_associd(stcb);

	/* initialize authentication parameters for the assoc */
	sctp_initialize_auth_params(inp, stcb);

	if (delay) {
		/* doing delayed connection */
		stcb->asoc.delayed_connection = 1;
		sctp_timer_start(SCTP_TIMER_TYPE_INIT, inp, stcb, stcb->asoc.primary_destination);
	} else {
		(void)SCTP_GETTIME_TIMEVAL(&stcb->asoc.time_entered);
		sctp_send_initiate(inp, stcb, SCTP_SO_LOCKED);
	}
	SCTP_TCB_UNLOCK(stcb);
	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) {
		stcb->sctp_ep->sctp_flags |= SCTP_PCB_FLAGS_CONNECTED;
		/* Set the connected flag so we can queue data */
		soisconnecting(so);
	}
 out_now:
	if (creat_lock_on) {
		SCTP_ASOC_CREATE_UNLOCK(inp);
	}
	SCTP_INP_DECR_REF(inp);
	return (error);
}

#define SCTP_FIND_STCB(inp, stcb, assoc_id) { \
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||\
	    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) { \
		SCTP_INP_RLOCK(inp); \
		stcb = LIST_FIRST(&inp->sctp_asoc_list); \
		if (stcb) { \
			SCTP_TCB_LOCK(stcb); \
                } \
		SCTP_INP_RUNLOCK(inp); \
	} else if (assoc_id > SCTP_ALL_ASSOC) { \
		stcb = sctp_findassociation_ep_asocid(inp, assoc_id, 1); \
		if (stcb == NULL) { \
		        SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOENT); \
			error = ENOENT; \
			break; \
		} \
	} else { \
		stcb = NULL; \
        } \
  }


#define SCTP_CHECK_AND_CAST(destp, srcp, type, size) {\
	if (size < sizeof(type)) { \
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL); \
		error = EINVAL; \
		break; \
	} else { \
		destp = (type *)srcp; \
	} \
      }

#if defined(__Panda__) || defined(__Userspace__)
int
#else
static int
#endif
sctp_getopt(struct socket *so, int optname, void *optval, size_t *optsize,
	    void *p) {
	struct sctp_inpcb *inp = NULL;
	int error, val = 0;
	struct sctp_tcb *stcb = NULL;

	if (optval == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (EINVAL);
	}

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return EINVAL;
	}
	error = 0;

	switch (optname) {
	case SCTP_NODELAY:
	case SCTP_AUTOCLOSE:
	case SCTP_EXPLICIT_EOR:
	case SCTP_AUTO_ASCONF:
	case SCTP_DISABLE_FRAGMENTS:
	case SCTP_I_WANT_MAPPED_V4_ADDR:
	case SCTP_USE_EXT_RCVINFO:
		SCTP_INP_RLOCK(inp);
		switch (optname) {
		case SCTP_DISABLE_FRAGMENTS:
			val = sctp_is_feature_on(inp, SCTP_PCB_FLAGS_NO_FRAGMENT);
			break;
		case SCTP_I_WANT_MAPPED_V4_ADDR:
			val = sctp_is_feature_on(inp, SCTP_PCB_FLAGS_NEEDS_MAPPED_V4);
			break;
		case SCTP_AUTO_ASCONF:
			if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
				/* only valid for bound all sockets */
				val = sctp_is_feature_on(inp, SCTP_PCB_FLAGS_AUTO_ASCONF);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
				goto flags_out;
			}
			break;
		case SCTP_EXPLICIT_EOR:
			val = sctp_is_feature_on(inp, SCTP_PCB_FLAGS_EXPLICIT_EOR);
			break;
		case SCTP_NODELAY:
			val = sctp_is_feature_on(inp, SCTP_PCB_FLAGS_NODELAY);
			break;
		case SCTP_USE_EXT_RCVINFO:
			val = sctp_is_feature_on(inp, SCTP_PCB_FLAGS_EXT_RCVINFO);
			break;
		case SCTP_AUTOCLOSE:
			if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_AUTOCLOSE))
				val = TICKS_TO_SEC(inp->sctp_ep.auto_close_time);
			else
				val = 0;
			break;

		default:
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOPROTOOPT);
			error = ENOPROTOOPT;
		} /* end switch (sopt->sopt_name) */
		if (*optsize < sizeof(val)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
		}
	flags_out:
		SCTP_INP_RUNLOCK(inp);
		if (error == 0) {
			/* return the option value */
			*(int *)optval = val;
			*optsize = sizeof(val);
		}
		break;
        case SCTP_GET_PACKET_LOG:
	{
#ifdef  SCTP_PACKET_LOGGING
		uint8_t *target;
		int ret;

		SCTP_CHECK_AND_CAST(target, optval, uint8_t, *optsize);
		ret = sctp_copy_out_packet_log(target , (int)*optsize);
		*optsize = ret;
#else
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EOPNOTSUPP);
		error = EOPNOTSUPP;
#endif
		break;
	}
	case SCTP_REUSE_PORT:
	{
		uint32_t *value;

		if ((inp->sctp_flags & SCTP_PCB_FLAGS_UDPTYPE)) {
			/* Can't do this for a 1-m socket */
			error = EINVAL;
			break;
		}
		SCTP_CHECK_AND_CAST(value, optval, uint32_t, *optsize);
		*value = sctp_is_feature_on(inp, SCTP_PCB_FLAGS_PORTREUSE);
		*optsize = sizeof(uint32_t);
		break;
	}
	case SCTP_PARTIAL_DELIVERY_POINT:
	{
		uint32_t *value;

		SCTP_CHECK_AND_CAST(value, optval, uint32_t, *optsize);
		*value = inp->partial_delivery_point;
		*optsize = sizeof(uint32_t);
		break;
	}
	case SCTP_FRAGMENT_INTERLEAVE:
	{
		uint32_t *value;

		SCTP_CHECK_AND_CAST(value, optval, uint32_t, *optsize);
		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_FRAG_INTERLEAVE)) {
			if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_INTERLEAVE_STRMS)) {
				*value = SCTP_FRAG_LEVEL_2;
			} else {
				*value = SCTP_FRAG_LEVEL_1;
			}
		} else {
			*value = SCTP_FRAG_LEVEL_0;
		}
		*optsize = sizeof(uint32_t);
		break;
	}
	case SCTP_CMT_ON_OFF:
	{
		struct sctp_assoc_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, *optsize);
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);
		if (stcb) {
			av->assoc_value = stcb->asoc.sctp_cmt_on_off;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				av->assoc_value = inp->sctp_cmt_on_off;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_assoc_value);
		}
		break;
	}
	case SCTP_PLUGGABLE_CC:
	{
		struct sctp_assoc_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, *optsize);
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);
		if (stcb) {
			av->assoc_value = stcb->asoc.congestion_control_module;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				av->assoc_value = inp->sctp_ep.sctp_default_cc_module;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_assoc_value);
		}
		break;
	}
	case SCTP_CC_OPTION:
	{
		struct sctp_cc_option *cc_opt;

		SCTP_CHECK_AND_CAST(cc_opt, optval, struct sctp_cc_option, *optsize);
		SCTP_FIND_STCB(inp, stcb, cc_opt->aid_value.assoc_id);
		if (stcb == NULL) {
			error = EINVAL;
		} else {
			if (stcb->asoc.cc_functions.sctp_cwnd_socket_option == NULL) {
				error = ENOTSUP;
			} else {
				error = (*stcb->asoc.cc_functions.sctp_cwnd_socket_option)(stcb, 0, cc_opt);
				*optsize = sizeof(struct sctp_cc_option);
			}
			SCTP_TCB_UNLOCK(stcb);
		}
		break;
	}
	case SCTP_PLUGGABLE_SS:
	{
		struct sctp_assoc_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, *optsize);
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);
		if (stcb) {
			av->assoc_value = stcb->asoc.stream_scheduling_module;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				av->assoc_value = inp->sctp_ep.sctp_default_ss_module;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_assoc_value);
		}
		break;
	}
	case SCTP_SS_VALUE:
	{
		struct sctp_stream_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_stream_value, *optsize);
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);
		if (stcb) {
			if (stcb->asoc.ss_functions.sctp_ss_get_value(stcb, &stcb->asoc, &stcb->asoc.strmout[av->stream_id],
			                                              &av->stream_value) < 0) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			} else {
				*optsize = sizeof(struct sctp_stream_value);
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			/* Can't get stream value without association */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
		}
		break;
	}
	case SCTP_GET_ADDR_LEN:
	{
		struct sctp_assoc_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, *optsize);
		error = EINVAL;
#ifdef INET
		if (av->assoc_value == AF_INET) {
			av->assoc_value = sizeof(struct sockaddr_in);
			error = 0;
		}
#endif
#ifdef INET6
		if (av->assoc_value == AF_INET6) {
			av->assoc_value = sizeof(struct sockaddr_in6);
			error = 0;
		}
#endif
#if defined(__Userspace__)
		if (av->assoc_value == AF_CONN) {
			av->assoc_value = sizeof(struct sockaddr_conn);
			error = 0;
		}
#endif
		if (error) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
		} else {
			*optsize = sizeof(struct sctp_assoc_value);
		}
		break;
	}
	case SCTP_GET_ASSOC_NUMBER:
	{
		uint32_t *value, cnt;

		SCTP_CHECK_AND_CAST(value, optval, uint32_t, *optsize);
		cnt = 0;
		SCTP_INP_RLOCK(inp);
		LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
			cnt++;
		}
		SCTP_INP_RUNLOCK(inp);
		*value = cnt;
		*optsize = sizeof(uint32_t);
		break;
	}
	case SCTP_GET_ASSOC_ID_LIST:
	{
		struct sctp_assoc_ids *ids;
		unsigned int at, limit;

		SCTP_CHECK_AND_CAST(ids, optval, struct sctp_assoc_ids, *optsize);
		at = 0;
		limit = (*optsize-sizeof(uint32_t))/ sizeof(sctp_assoc_t);
		SCTP_INP_RLOCK(inp);
		LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
			if (at < limit) {
				ids->gaids_assoc_id[at++] = sctp_get_associd(stcb);
			} else {
				error = EINVAL;
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
				break;
			}
		}
		SCTP_INP_RUNLOCK(inp);
		if (error == 0) {
			ids->gaids_number_of_ids = at;
			*optsize = ((at * sizeof(sctp_assoc_t)) + sizeof(uint32_t));
		}
		break;
	}
	case SCTP_CONTEXT:
	{
		struct sctp_assoc_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, *optsize);
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);

		if (stcb) {
			av->assoc_value = stcb->asoc.context;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				av->assoc_value = inp->sctp_context;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_assoc_value);
		}
		break;
	}
	case SCTP_VRF_ID:
	{
		uint32_t *default_vrfid;

		SCTP_CHECK_AND_CAST(default_vrfid, optval, uint32_t, *optsize);
		*default_vrfid = inp->def_vrf_id;
		*optsize = sizeof(uint32_t);
		break;
	}
	case SCTP_GET_ASOC_VRF:
	{
		struct sctp_assoc_value *id;

		SCTP_CHECK_AND_CAST(id, optval, struct sctp_assoc_value, *optsize);
		SCTP_FIND_STCB(inp, stcb, id->assoc_id);
		if (stcb == NULL) {
			error = EINVAL;
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
		} else {
			id->assoc_value = stcb->asoc.vrf_id;
			*optsize = sizeof(struct sctp_assoc_value);
		}
		break;
	}
	case SCTP_GET_VRF_IDS:
	{
#ifdef SCTP_MVRF
		int siz_needed;
		uint32_t *vrf_ids;

		SCTP_CHECK_AND_CAST(vrf_ids, optval, uint32_t, *optsize);
		siz_needed = inp->num_vrfs * sizeof(uint32_t);
		if (*optsize < siz_needed) {
			error = EINVAL;
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
		} else {
			memcpy(vrf_ids, inp->m_vrf_ids, siz_needed);
			*optsize = siz_needed;
		}
#else
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EOPNOTSUPP);
		error = EOPNOTSUPP;
#endif
		break;
	}
	case SCTP_GET_NONCE_VALUES:
	{
		struct sctp_get_nonce_values *gnv;

		SCTP_CHECK_AND_CAST(gnv, optval, struct sctp_get_nonce_values, *optsize);
		SCTP_FIND_STCB(inp, stcb, gnv->gn_assoc_id);

		if (stcb) {
			gnv->gn_peers_tag = stcb->asoc.peer_vtag;
			gnv->gn_local_tag = stcb->asoc.my_vtag;
			SCTP_TCB_UNLOCK(stcb);
			*optsize = sizeof(struct sctp_get_nonce_values);
		} else {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOTCONN);
			error = ENOTCONN;
		}
		break;
	}
	case SCTP_DELAYED_SACK:
	{
		struct sctp_sack_info *sack;

		SCTP_CHECK_AND_CAST(sack, optval, struct sctp_sack_info, *optsize);
		SCTP_FIND_STCB(inp, stcb, sack->sack_assoc_id);
		if (stcb) {
			sack->sack_delay = stcb->asoc.delayed_ack;
			sack->sack_freq = stcb->asoc.sack_freq;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (sack->sack_assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				sack->sack_delay = TICKS_TO_MSEC(inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_RECV]);
				sack->sack_freq = inp->sctp_ep.sctp_sack_freq;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_sack_info);
		}
		break;
	}
	case SCTP_GET_SNDBUF_USE:
	{
		struct sctp_sockstat *ss;

		SCTP_CHECK_AND_CAST(ss, optval, struct sctp_sockstat, *optsize);
		SCTP_FIND_STCB(inp, stcb, ss->ss_assoc_id);

		if (stcb) {
			ss->ss_total_sndbuf = stcb->asoc.total_output_queue_size;
			ss->ss_total_recv_buf = (stcb->asoc.size_on_reasm_queue +
						 stcb->asoc.size_on_all_streams);
			SCTP_TCB_UNLOCK(stcb);
			*optsize = sizeof(struct sctp_sockstat);
		} else {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOTCONN);
			error = ENOTCONN;
		}
		break;
	}
	case SCTP_MAX_BURST:
	{
#if defined(__FreeBSD__) && __FreeBSD_version < 900000
		uint8_t *value;

		SCTP_CHECK_AND_CAST(value, optval, uint8_t, *optsize);

		SCTP_INP_RLOCK(inp);
		if (inp->sctp_ep.max_burst < 256) {
			*value = inp->sctp_ep.max_burst;
		} else {
			*value = 255;
		}
		SCTP_INP_RUNLOCK(inp);
		*optsize = sizeof(uint8_t);
#else
		struct sctp_assoc_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, *optsize);
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);

		if (stcb) {
			av->assoc_value = stcb->asoc.max_burst;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				av->assoc_value = inp->sctp_ep.max_burst;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_assoc_value);
		}
#endif
		break;
	}
	case SCTP_MAXSEG:
	{
		struct sctp_assoc_value *av;
		int ovh;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, *optsize);
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);

		if (stcb) {
			av->assoc_value = sctp_get_frag_point(stcb, &stcb->asoc);
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
					ovh = SCTP_MED_OVERHEAD;
				} else {
					ovh = SCTP_MED_V4_OVERHEAD;
				}
				if (inp->sctp_frag_point >= SCTP_DEFAULT_MAXSEGMENT)
					av->assoc_value = 0;
				else
					av->assoc_value = inp->sctp_frag_point - ovh;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_assoc_value);
		}
		break;
	}
	case SCTP_GET_STAT_LOG:
		error = sctp_fill_stat_log(optval, optsize);
		break;
	case SCTP_EVENTS:
	{
		struct sctp_event_subscribe *events;

		SCTP_CHECK_AND_CAST(events, optval, struct sctp_event_subscribe, *optsize);
		memset(events, 0, sizeof(struct sctp_event_subscribe));
		SCTP_INP_RLOCK(inp);
		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVDATAIOEVNT))
			events->sctp_data_io_event = 1;

		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVASSOCEVNT))
			events->sctp_association_event = 1;

		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVPADDREVNT))
			events->sctp_address_event = 1;

		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVSENDFAILEVNT))
			events->sctp_send_failure_event = 1;

		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVPEERERR))
			events->sctp_peer_error_event = 1;

		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVSHUTDOWNEVNT))
			events->sctp_shutdown_event = 1;

		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_PDAPIEVNT))
			events->sctp_partial_delivery_event = 1;

		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_ADAPTATIONEVNT))
			events->sctp_adaptation_layer_event = 1;

		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_AUTHEVNT))
			events->sctp_authentication_event = 1;

		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_DRYEVNT))
			events->sctp_sender_dry_event = 1;

		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_STREAM_RESETEVNT))
			events->sctp_stream_reset_event = 1;
		SCTP_INP_RUNLOCK(inp);
		*optsize = sizeof(struct sctp_event_subscribe);
		break;
	}
	case SCTP_ADAPTATION_LAYER:
	{
		uint32_t *value;

		SCTP_CHECK_AND_CAST(value, optval, uint32_t, *optsize);

		SCTP_INP_RLOCK(inp);
		*value = inp->sctp_ep.adaptation_layer_indicator;
		SCTP_INP_RUNLOCK(inp);
		*optsize = sizeof(uint32_t);
		break;
	}
	case SCTP_SET_INITIAL_DBG_SEQ:
	{
		uint32_t *value;

		SCTP_CHECK_AND_CAST(value, optval, uint32_t, *optsize);
		SCTP_INP_RLOCK(inp);
		*value = inp->sctp_ep.initial_sequence_debug;
		SCTP_INP_RUNLOCK(inp);
		*optsize = sizeof(uint32_t);
		break;
	}
	case SCTP_GET_LOCAL_ADDR_SIZE:
	{
		uint32_t *value;

		SCTP_CHECK_AND_CAST(value, optval, uint32_t, *optsize);
		SCTP_INP_RLOCK(inp);
		*value = sctp_count_max_addresses(inp);
		SCTP_INP_RUNLOCK(inp);
		*optsize = sizeof(uint32_t);
		break;
	}
	case SCTP_GET_REMOTE_ADDR_SIZE:
	{
		uint32_t *value;
		size_t size;
		struct sctp_nets *net;

		SCTP_CHECK_AND_CAST(value, optval, uint32_t, *optsize);
		/* FIXME MT: change to sctp_assoc_value? */
		SCTP_FIND_STCB(inp, stcb, (sctp_assoc_t) *value);

		if (stcb) {
			size = 0;
			/* Count the sizes */
			TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
				if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_NEEDS_MAPPED_V4)) {
					size += sizeof(struct sockaddr_in6);
				} else {
					switch (((struct sockaddr *)&net->ro._l_addr)->sa_family) {
#ifdef INET
					case AF_INET:
						size += sizeof(struct sockaddr_in);
						break;
#endif
#ifdef INET6
					case AF_INET6:
						size += sizeof(struct sockaddr_in6);
						break;
#endif
#if defined(__Userspace__)
					case AF_CONN:
						size += sizeof(struct sockaddr_conn);
						break;
#endif
					default:
						break;
					}
				}
			}
			SCTP_TCB_UNLOCK(stcb);
			*value = (uint32_t) size;
			*optsize = sizeof(uint32_t);
		} else {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOTCONN);
			error = ENOTCONN;
		}
		break;
	}
	case SCTP_GET_PEER_ADDRESSES:
		/*
		 * Get the address information, an array is passed in to
		 * fill up we pack it.
		 */
	{
		size_t cpsz, left;
		struct sockaddr_storage *sas;
		struct sctp_nets *net;
		struct sctp_getaddresses *saddr;

		SCTP_CHECK_AND_CAST(saddr, optval, struct sctp_getaddresses, *optsize);
		SCTP_FIND_STCB(inp, stcb, saddr->sget_assoc_id);

		if (stcb) {
			left = (*optsize) - sizeof(struct sctp_getaddresses);
			*optsize = sizeof(struct sctp_getaddresses);
			sas = (struct sockaddr_storage *)&saddr->addr[0];

			TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
				if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_NEEDS_MAPPED_V4)) {
					cpsz = sizeof(struct sockaddr_in6);
				} else {
					switch (((struct sockaddr *)&net->ro._l_addr)->sa_family) {
#ifdef INET
					case AF_INET:
						cpsz = sizeof(struct sockaddr_in);
						break;
#endif
#ifdef INET6
					case AF_INET6:
						cpsz = sizeof(struct sockaddr_in6);
						break;
#endif
#if defined(__Userspace__)
					case AF_CONN:
						cpsz = sizeof(struct sockaddr_conn);
						break;
#endif
					default:
						cpsz = 0;
						break;
					}
				}
				if (cpsz == 0) {
					break;
				}
				if (left < cpsz) {
					/* not enough room. */
					break;
				}
#if defined(INET) && defined(INET6)
				if ((sctp_is_feature_on(inp, SCTP_PCB_FLAGS_NEEDS_MAPPED_V4)) &&
				    (((struct sockaddr *)&net->ro._l_addr)->sa_family == AF_INET)) {
					/* Must map the address */
					in6_sin_2_v4mapsin6((struct sockaddr_in *)&net->ro._l_addr,
							    (struct sockaddr_in6 *)sas);
				} else {
#endif
					memcpy(sas, &net->ro._l_addr, cpsz);
#if defined(INET) && defined(INET6)
				}
#endif
				((struct sockaddr_in *)sas)->sin_port = stcb->rport;

				sas = (struct sockaddr_storage *)((caddr_t)sas + cpsz);
				left -= cpsz;
				*optsize += cpsz;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
		        SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOENT);
			error = ENOENT;
		}
		break;
	}
	case SCTP_GET_LOCAL_ADDRESSES:
	{
		size_t limit, actual;
		struct sockaddr_storage *sas;
		struct sctp_getaddresses *saddr;

		SCTP_CHECK_AND_CAST(saddr, optval, struct sctp_getaddresses, *optsize);
		SCTP_FIND_STCB(inp, stcb, saddr->sget_assoc_id);

		sas = (struct sockaddr_storage *)&saddr->addr[0];
		limit = *optsize - sizeof(sctp_assoc_t);
		actual = sctp_fill_up_addresses(inp, stcb, limit, sas);
		if (stcb) {
			SCTP_TCB_UNLOCK(stcb);
		}
		*optsize = sizeof(struct sockaddr_storage) + actual;
		break;
	}
	case SCTP_PEER_ADDR_PARAMS:
	{
		struct sctp_paddrparams *paddrp;
		struct sctp_nets *net;

		SCTP_CHECK_AND_CAST(paddrp, optval, struct sctp_paddrparams, *optsize);
		SCTP_FIND_STCB(inp, stcb, paddrp->spp_assoc_id);

		net = NULL;
		if (stcb) {
			net = sctp_findnet(stcb, (struct sockaddr *)&paddrp->spp_address);
		} else {
			/* We increment here since sctp_findassociation_ep_addr() wil
			 * do a decrement if it finds the stcb as long as the locked
			 * tcb (last argument) is NOT a TCB.. aka NULL.
			 */
			SCTP_INP_INCR_REF(inp);
			stcb = sctp_findassociation_ep_addr(&inp, (struct sockaddr *)&paddrp->spp_address, &net, NULL, NULL);
			if (stcb == NULL) {
				SCTP_INP_DECR_REF(inp);
			}
		}
		if (stcb && (net == NULL)) {
			struct sockaddr *sa;

			sa = (struct sockaddr *)&paddrp->spp_address;
#ifdef INET
			if (sa->sa_family == AF_INET) {
				struct sockaddr_in *sin;

				sin = (struct sockaddr_in *)sa;
				if (sin->sin_addr.s_addr) {
					error = EINVAL;
					SCTP_TCB_UNLOCK(stcb);
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
					break;
				}
			} else
#endif
#ifdef INET6
			if (sa->sa_family == AF_INET6) {
				struct sockaddr_in6 *sin6;

				sin6 = (struct sockaddr_in6 *)sa;
				if (!IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
					error = EINVAL;
					SCTP_TCB_UNLOCK(stcb);
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
					break;
				}
			} else
#endif
#if defined(__Userspace__)
			if (sa->sa_family == AF_CONN) {
				struct sockaddr_conn *sconn;

				sconn = (struct sockaddr_conn *)sa;
				if (sconn->sconn_addr != NULL) {
					error = EINVAL;
					SCTP_TCB_UNLOCK(stcb);
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
					break;
				}
			} else
#endif
			{
				error = EAFNOSUPPORT;
				SCTP_TCB_UNLOCK(stcb);
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
				break;
			}
		}

		if (stcb) {
			/* Applies to the specific association */
			paddrp->spp_flags = 0;
			if (net) {
				int ovh;
				if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
					ovh = SCTP_MED_OVERHEAD;
				} else {
					ovh = SCTP_MED_V4_OVERHEAD;
				}

				paddrp->spp_hbinterval = net->heart_beat_delay;
				paddrp->spp_pathmaxrxt = net->failure_threshold;
				paddrp->spp_pathmtu = net->mtu - ovh;
				/* get flags for HB */
				if (net->dest_state & SCTP_ADDR_NOHB) {
					paddrp->spp_flags |= SPP_HB_DISABLE;
				} else {
					paddrp->spp_flags |= SPP_HB_ENABLE;
				}
				/* get flags for PMTU */
				if (net->dest_state & SCTP_ADDR_NO_PMTUD) {
					paddrp->spp_flags |= SPP_PMTUD_ENABLE;
				} else {
					paddrp->spp_flags |= SPP_PMTUD_DISABLE;
				}
				if (net->dscp & 0x01) {
					paddrp->spp_dscp = net->dscp & 0xfc;
					paddrp->spp_flags |= SPP_DSCP;
				}
#ifdef INET6
				if ((net->ro._l_addr.sa.sa_family == AF_INET6) &&
				    (net->flowlabel & 0x80000000)) {
					paddrp->spp_ipv6_flowlabel = net->flowlabel & 0x000fffff;
					paddrp->spp_flags |= SPP_IPV6_FLOWLABEL;
				}
#endif
			} else {
				/*
				 * No destination so return default
				 * value
				 */
				paddrp->spp_pathmaxrxt = stcb->asoc.def_net_failure;
				paddrp->spp_pathmtu = sctp_get_frag_point(stcb, &stcb->asoc);
				if (stcb->asoc.default_dscp & 0x01) {
					paddrp->spp_dscp = stcb->asoc.default_dscp & 0xfc;
					paddrp->spp_flags |= SPP_DSCP;
				}
#ifdef INET6
				if (stcb->asoc.default_flowlabel & 0x80000000) {
					paddrp->spp_ipv6_flowlabel = stcb->asoc.default_flowlabel & 0x000fffff;
					paddrp->spp_flags |= SPP_IPV6_FLOWLABEL;
				}
#endif
				/* default settings should be these */
				if (sctp_stcb_is_feature_on(inp, stcb, SCTP_PCB_FLAGS_DONOT_HEARTBEAT)) {
					paddrp->spp_flags |= SPP_HB_DISABLE;
				} else {
					paddrp->spp_flags |= SPP_HB_ENABLE;
				}
				if (sctp_stcb_is_feature_on(inp, stcb, SCTP_PCB_FLAGS_DO_NOT_PMTUD)) {
					paddrp->spp_flags |= SPP_PMTUD_DISABLE;
				} else {
					paddrp->spp_flags |= SPP_PMTUD_ENABLE;
				}
				paddrp->spp_hbinterval = stcb->asoc.heart_beat_delay;
			}
			paddrp->spp_assoc_id = sctp_get_associd(stcb);
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (paddrp->spp_assoc_id == SCTP_FUTURE_ASSOC)) {
				/* Use endpoint defaults */
				SCTP_INP_RLOCK(inp);
				paddrp->spp_pathmaxrxt = inp->sctp_ep.def_net_failure;
				paddrp->spp_hbinterval = TICKS_TO_MSEC(inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_HEARTBEAT]);
				paddrp->spp_assoc_id = SCTP_FUTURE_ASSOC;
				/* get inp's default */
				if (inp->sctp_ep.default_dscp & 0x01) {
					paddrp->spp_dscp = inp->sctp_ep.default_dscp & 0xfc;
					paddrp->spp_flags |= SPP_DSCP;
				}
#ifdef INET6
				if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
				    (inp->sctp_ep.default_flowlabel & 0x80000000)) {
					paddrp->spp_ipv6_flowlabel = inp->sctp_ep.default_flowlabel & 0x000fffff;
					paddrp->spp_flags |= SPP_IPV6_FLOWLABEL;
				}
#endif
				/* can't return this */
				paddrp->spp_pathmtu = 0;

				if (sctp_is_feature_off(inp, SCTP_PCB_FLAGS_DONOT_HEARTBEAT)) {
					paddrp->spp_flags |= SPP_HB_ENABLE;
				} else {
					paddrp->spp_flags |= SPP_HB_DISABLE;
				}
				if (sctp_is_feature_off(inp, SCTP_PCB_FLAGS_DO_NOT_PMTUD)) {
					paddrp->spp_flags |= SPP_PMTUD_ENABLE;
				} else {
					paddrp->spp_flags |= SPP_PMTUD_DISABLE;
				}
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_paddrparams);
		}
		break;
	}
	case SCTP_GET_PEER_ADDR_INFO:
	{
		struct sctp_paddrinfo *paddri;
		struct sctp_nets *net;

		SCTP_CHECK_AND_CAST(paddri, optval, struct sctp_paddrinfo, *optsize);
		SCTP_FIND_STCB(inp, stcb, paddri->spinfo_assoc_id);

		net = NULL;
		if (stcb) {
			net = sctp_findnet(stcb, (struct sockaddr *)&paddri->spinfo_address);
		} else {
			/* We increment here since sctp_findassociation_ep_addr() wil
			 * do a decrement if it finds the stcb as long as the locked
			 * tcb (last argument) is NOT a TCB.. aka NULL.
			 */
			SCTP_INP_INCR_REF(inp);
			stcb = sctp_findassociation_ep_addr(&inp, (struct sockaddr *)&paddri->spinfo_address, &net, NULL, NULL);
			if (stcb == NULL) {
				SCTP_INP_DECR_REF(inp);
			}
		}

		if ((stcb) && (net)) {
			if (net->dest_state & SCTP_ADDR_UNCONFIRMED) {
				/* It's unconfirmed */
				paddri->spinfo_state = SCTP_UNCONFIRMED;
			} else if (net->dest_state & SCTP_ADDR_REACHABLE) {
				/* It's active */
				paddri->spinfo_state = SCTP_ACTIVE;
			} else {
				/* It's inactive */
				paddri->spinfo_state = SCTP_INACTIVE;
			}
			paddri->spinfo_cwnd = net->cwnd;
			paddri->spinfo_srtt = net->lastsa >> SCTP_RTT_SHIFT;
			paddri->spinfo_rto = net->RTO;
			paddri->spinfo_assoc_id = sctp_get_associd(stcb);
			paddri->spinfo_mtu = net->mtu;
			SCTP_TCB_UNLOCK(stcb);
			*optsize = sizeof(struct sctp_paddrinfo);
		} else {
			if (stcb) {
				SCTP_TCB_UNLOCK(stcb);
			}
		        SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOENT);
			error = ENOENT;
		}
		break;
	}
	case SCTP_PCB_STATUS:
	{
		struct sctp_pcbinfo *spcb;

		SCTP_CHECK_AND_CAST(spcb, optval, struct sctp_pcbinfo, *optsize);
		sctp_fill_pcbinfo(spcb);
		*optsize = sizeof(struct sctp_pcbinfo);
		break;
	}
	case SCTP_STATUS:
	{
		struct sctp_nets *net;
		struct sctp_status *sstat;

		SCTP_CHECK_AND_CAST(sstat, optval, struct sctp_status, *optsize);
		SCTP_FIND_STCB(inp, stcb, sstat->sstat_assoc_id);

		if (stcb == NULL) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		/*
		 * I think passing the state is fine since
		 * sctp_constants.h will be available to the user
		 * land.
		 */
		sstat->sstat_state = stcb->asoc.state;
		sstat->sstat_assoc_id = sctp_get_associd(stcb);
		sstat->sstat_rwnd = stcb->asoc.peers_rwnd;
		sstat->sstat_unackdata = stcb->asoc.sent_queue_cnt;
		/*
		 * We can't include chunks that have been passed to
		 * the socket layer. Only things in queue.
		 */
		sstat->sstat_penddata = (stcb->asoc.cnt_on_reasm_queue +
					 stcb->asoc.cnt_on_all_streams);


		sstat->sstat_instrms = stcb->asoc.streamincnt;
		sstat->sstat_outstrms = stcb->asoc.streamoutcnt;
		sstat->sstat_fragmentation_point = sctp_get_frag_point(stcb, &stcb->asoc);
#ifdef HAVE_SA_LEN
		memcpy(&sstat->sstat_primary.spinfo_address,
		       &stcb->asoc.primary_destination->ro._l_addr,
		       ((struct sockaddr *)(&stcb->asoc.primary_destination->ro._l_addr))->sa_len);
#else
		if (stcb->asoc.primary_destination->ro._l_addr.sa.sa_family == AF_INET) {
			memcpy(&sstat->sstat_primary.spinfo_address,
			       &stcb->asoc.primary_destination->ro._l_addr,
			       sizeof(struct sockaddr_in));
		} else {
			memcpy(&sstat->sstat_primary.spinfo_address,
			       &stcb->asoc.primary_destination->ro._l_addr,
			       sizeof(struct sockaddr_in6));
		}
#endif
		net = stcb->asoc.primary_destination;
		((struct sockaddr_in *)&sstat->sstat_primary.spinfo_address)->sin_port = stcb->rport;
		/*
		 * Again the user can get info from sctp_constants.h
		 * for what the state of the network is.
		 */
		if (net->dest_state & SCTP_ADDR_UNCONFIRMED) {
			/* It's unconfirmed */
			sstat->sstat_primary.spinfo_state = SCTP_UNCONFIRMED;
		} else if (net->dest_state & SCTP_ADDR_REACHABLE) {
			/* It's active */
			sstat->sstat_primary.spinfo_state = SCTP_ACTIVE;
		} else {
			/* It's inactive */
			sstat->sstat_primary.spinfo_state = SCTP_INACTIVE;
		}
		sstat->sstat_primary.spinfo_cwnd = net->cwnd;
		sstat->sstat_primary.spinfo_srtt = net->lastsa >> SCTP_RTT_SHIFT;
		sstat->sstat_primary.spinfo_rto = net->RTO;
		sstat->sstat_primary.spinfo_mtu = net->mtu;
		sstat->sstat_primary.spinfo_assoc_id = sctp_get_associd(stcb);
		SCTP_TCB_UNLOCK(stcb);
		*optsize = sizeof(struct sctp_status);
		break;
	}
	case SCTP_RTOINFO:
	{
		struct sctp_rtoinfo *srto;

		SCTP_CHECK_AND_CAST(srto, optval, struct sctp_rtoinfo, *optsize);
		SCTP_FIND_STCB(inp, stcb, srto->srto_assoc_id);

		if (stcb) {
			srto->srto_initial = stcb->asoc.initial_rto;
			srto->srto_max = stcb->asoc.maxrto;
			srto->srto_min = stcb->asoc.minrto;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (srto->srto_assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				srto->srto_initial = inp->sctp_ep.initial_rto;
				srto->srto_max = inp->sctp_ep.sctp_maxrto;
				srto->srto_min = inp->sctp_ep.sctp_minrto;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_rtoinfo);
		}
		break;
	}
	case SCTP_TIMEOUTS:
	{
		struct sctp_timeouts *stimo;

		SCTP_CHECK_AND_CAST(stimo, optval, struct sctp_timeouts, *optsize);
		SCTP_FIND_STCB(inp, stcb, stimo->stimo_assoc_id);

		if (stcb) {
			stimo->stimo_init= stcb->asoc.timoinit;
			stimo->stimo_data= stcb->asoc.timodata;
			stimo->stimo_sack= stcb->asoc.timosack;
			stimo->stimo_shutdown= stcb->asoc.timoshutdown;
			stimo->stimo_heartbeat= stcb->asoc.timoheartbeat;
			stimo->stimo_cookie= stcb->asoc.timocookie;
			stimo->stimo_shutdownack= stcb->asoc.timoshutdownack;
			SCTP_TCB_UNLOCK(stcb);
			*optsize = sizeof(struct sctp_timeouts);
		} else {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
		}
		break;
	}
	case SCTP_ASSOCINFO:
	{
		struct sctp_assocparams *sasoc;

		SCTP_CHECK_AND_CAST(sasoc, optval, struct sctp_assocparams, *optsize);
		SCTP_FIND_STCB(inp, stcb, sasoc->sasoc_assoc_id);

		if (stcb) {
			sasoc->sasoc_cookie_life = TICKS_TO_MSEC(stcb->asoc.cookie_life);
			sasoc->sasoc_asocmaxrxt = stcb->asoc.max_send_times;
			sasoc->sasoc_number_peer_destinations = stcb->asoc.numnets;
			sasoc->sasoc_peer_rwnd = stcb->asoc.peers_rwnd;
			sasoc->sasoc_local_rwnd = stcb->asoc.my_rwnd;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (sasoc->sasoc_assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				sasoc->sasoc_cookie_life = TICKS_TO_MSEC(inp->sctp_ep.def_cookie_life);
				sasoc->sasoc_asocmaxrxt = inp->sctp_ep.max_send_times;
				sasoc->sasoc_number_peer_destinations = 0;
				sasoc->sasoc_peer_rwnd = 0;
				sasoc->sasoc_local_rwnd = sbspace(&inp->sctp_socket->so_rcv);
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_assocparams);
		}
		break;
	}
	case SCTP_DEFAULT_SEND_PARAM:
	{
		struct sctp_sndrcvinfo *s_info;

		SCTP_CHECK_AND_CAST(s_info, optval, struct sctp_sndrcvinfo, *optsize);
		SCTP_FIND_STCB(inp, stcb, s_info->sinfo_assoc_id);

		if (stcb) {
			memcpy(s_info, &stcb->asoc.def_send, sizeof(stcb->asoc.def_send));
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (s_info->sinfo_assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				memcpy(s_info, &inp->def_send, sizeof(inp->def_send));
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_sndrcvinfo);
		}
		break;
	}
	case SCTP_INITMSG:
	{
		struct sctp_initmsg *sinit;

		SCTP_CHECK_AND_CAST(sinit, optval, struct sctp_initmsg, *optsize);
		SCTP_INP_RLOCK(inp);
		sinit->sinit_num_ostreams = inp->sctp_ep.pre_open_stream_count;
		sinit->sinit_max_instreams = inp->sctp_ep.max_open_streams_intome;
		sinit->sinit_max_attempts = inp->sctp_ep.max_init_times;
		sinit->sinit_max_init_timeo = inp->sctp_ep.initial_init_rto_max;
		SCTP_INP_RUNLOCK(inp);
		*optsize = sizeof(struct sctp_initmsg);
		break;
	}
	case SCTP_PRIMARY_ADDR:
		/* we allow a "get" operation on this */
	{
		struct sctp_setprim *ssp;

		SCTP_CHECK_AND_CAST(ssp, optval, struct sctp_setprim, *optsize);
		SCTP_FIND_STCB(inp, stcb, ssp->ssp_assoc_id);

		if (stcb) {
			/* simply copy out the sockaddr_storage... */
			int len;

			len = *optsize;
#ifdef HAVE_SA_LEN
			if (len > stcb->asoc.primary_destination->ro._l_addr.sa.sa_len)
				len = stcb->asoc.primary_destination->ro._l_addr.sa.sa_len;
#else
			if (stcb->asoc.primary_destination->ro._l_addr.sa.sa_family == AF_INET &&
			    len > sizeof(struct sockaddr_in))
				len = sizeof(struct sockaddr_in);
			else if (
			    stcb->asoc.primary_destination->ro._l_addr.sa.sa_family == AF_INET6 &&
			    len > sizeof(struct sockaddr_in6))
				len = sizeof(struct sockaddr_in6);
#endif

			memcpy(&ssp->ssp_addr,
			       &stcb->asoc.primary_destination->ro._l_addr,
			       len);
			SCTP_TCB_UNLOCK(stcb);
			*optsize = sizeof(struct sctp_setprim);
		} else {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
		}
		break;
	}
	case SCTP_HMAC_IDENT:
	{
		struct sctp_hmacalgo *shmac;
		sctp_hmaclist_t *hmaclist;
		uint32_t size;
		int i;

		SCTP_CHECK_AND_CAST(shmac, optval, struct sctp_hmacalgo, *optsize);

		SCTP_INP_RLOCK(inp);
		hmaclist = inp->sctp_ep.local_hmacs;
		if (hmaclist == NULL) {
			/* no HMACs to return */
			*optsize = sizeof(*shmac);
			SCTP_INP_RUNLOCK(inp);
			break;
		}
		/* is there room for all of the hmac ids? */
		size = sizeof(*shmac) + (hmaclist->num_algo *
					 sizeof(shmac->shmac_idents[0]));
		if ((size_t)(*optsize) < size) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			SCTP_INP_RUNLOCK(inp);
			break;
		}
		/* copy in the list */
		shmac->shmac_number_of_idents = hmaclist->num_algo;
		for (i = 0; i < hmaclist->num_algo; i++) {
			shmac->shmac_idents[i] = hmaclist->hmac[i];
		}
		SCTP_INP_RUNLOCK(inp);
		*optsize = size;
		break;
	}
	case SCTP_AUTH_ACTIVE_KEY:
	{
		struct sctp_authkeyid *scact;

		SCTP_CHECK_AND_CAST(scact, optval, struct sctp_authkeyid, *optsize);
		SCTP_FIND_STCB(inp, stcb, scact->scact_assoc_id);

		if (stcb) {
			/* get the active key on the assoc */
			scact->scact_keynumber = stcb->asoc.authinfo.active_keyid;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (scact->scact_assoc_id == SCTP_FUTURE_ASSOC)) {
				/* get the endpoint active key */
				SCTP_INP_RLOCK(inp);
				scact->scact_keynumber = inp->sctp_ep.default_keyid;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_authkeyid);
		}
		break;
	}
	case SCTP_LOCAL_AUTH_CHUNKS:
	{
		struct sctp_authchunks *sac;
		sctp_auth_chklist_t *chklist = NULL;
		size_t size = 0;

		SCTP_CHECK_AND_CAST(sac, optval, struct sctp_authchunks, *optsize);
		SCTP_FIND_STCB(inp, stcb, sac->gauth_assoc_id);

		if (stcb) {
			/* get off the assoc */
			chklist = stcb->asoc.local_auth_chunks;
			/* is there enough space? */
			size = sctp_auth_get_chklist_size(chklist);
			if (*optsize < (sizeof(struct sctp_authchunks) + size)) {
				error = EINVAL;
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
			} else {
				/* copy in the chunks */
				(void)sctp_serialize_auth_chunks(chklist, sac->gauth_chunks);
				sac->gauth_number_of_chunks = (uint32_t)size;
				*optsize = sizeof(struct sctp_authchunks) + size;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (sac->gauth_assoc_id == SCTP_FUTURE_ASSOC)) {
				/* get off the endpoint */
				SCTP_INP_RLOCK(inp);
				chklist = inp->sctp_ep.local_auth_chunks;
				/* is there enough space? */
				size = sctp_auth_get_chklist_size(chklist);
				if (*optsize < (sizeof(struct sctp_authchunks) + size)) {
					error = EINVAL;
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
				} else {
					/* copy in the chunks */
					(void)sctp_serialize_auth_chunks(chklist, sac->gauth_chunks);
					sac->gauth_number_of_chunks = (uint32_t)size;
					*optsize = sizeof(struct sctp_authchunks) + size;
				}
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		break;
	}
	case SCTP_PEER_AUTH_CHUNKS:
	{
		struct sctp_authchunks *sac;
		sctp_auth_chklist_t *chklist = NULL;
		size_t size = 0;

		SCTP_CHECK_AND_CAST(sac, optval, struct sctp_authchunks, *optsize);
		SCTP_FIND_STCB(inp, stcb, sac->gauth_assoc_id);

		if (stcb) {
			/* get off the assoc */
			chklist = stcb->asoc.peer_auth_chunks;
			/* is there enough space? */
			size = sctp_auth_get_chklist_size(chklist);
			if (*optsize < (sizeof(struct sctp_authchunks) + size)) {
				error = EINVAL;
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
			} else {
				/* copy in the chunks */
				(void)sctp_serialize_auth_chunks(chklist, sac->gauth_chunks);
				sac->gauth_number_of_chunks = (uint32_t)size;
				*optsize = sizeof(struct sctp_authchunks) + size;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
		        SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOENT);
			error = ENOENT;
		}
		break;
	}
#if defined(HAVE_SCTP_PEELOFF_SOCKOPT)
	case SCTP_PEELOFF:
	{
		struct sctp_peeloff_opt *peeloff;

		SCTP_CHECK_AND_CAST(peeloff, optval, struct sctp_peeloff_opt, *optsize);
		/* do the peeloff */
		error = sctp_peeloff_option(p, peeloff);
		if (error == 0) {
			*optsize = sizeof(struct sctp_peeloff_opt);
		}
	}
	break;
#endif /* HAVE_SCTP_PEELOFF_SOCKOPT */
	case SCTP_EVENT:
	{
		struct sctp_event *event;
		uint32_t event_type;

		SCTP_CHECK_AND_CAST(event, optval, struct sctp_event, *optsize);
		SCTP_FIND_STCB(inp, stcb, event->se_assoc_id);

		switch (event->se_type) {
		case SCTP_ASSOC_CHANGE:
			event_type = SCTP_PCB_FLAGS_RECVASSOCEVNT;
			break;
		case SCTP_PEER_ADDR_CHANGE:
			event_type = SCTP_PCB_FLAGS_RECVPADDREVNT;
			break;
		case SCTP_REMOTE_ERROR:
			event_type = SCTP_PCB_FLAGS_RECVPEERERR;
			break;
		case SCTP_SEND_FAILED:
			event_type = SCTP_PCB_FLAGS_RECVSENDFAILEVNT;
			break;
		case SCTP_SHUTDOWN_EVENT:
			event_type = SCTP_PCB_FLAGS_RECVSHUTDOWNEVNT;
			break;
		case SCTP_ADAPTATION_INDICATION:
			event_type = SCTP_PCB_FLAGS_ADAPTATIONEVNT;
			break;
		case SCTP_PARTIAL_DELIVERY_EVENT:
			event_type = SCTP_PCB_FLAGS_PDAPIEVNT;
			break;
		case SCTP_AUTHENTICATION_EVENT:
			event_type = SCTP_PCB_FLAGS_AUTHEVNT;
			break;
		case SCTP_STREAM_RESET_EVENT:
			event_type = SCTP_PCB_FLAGS_STREAM_RESETEVNT;
			break;
		case SCTP_SENDER_DRY_EVENT:
			event_type = SCTP_PCB_FLAGS_DRYEVNT;
			break;
		case SCTP_NOTIFICATIONS_STOPPED_EVENT:
			event_type = 0;
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOTSUP);
			error = ENOTSUP;
			break;
		case SCTP_ASSOC_RESET_EVENT:
			event_type = SCTP_PCB_FLAGS_ASSOC_RESETEVNT;
			break;
		case SCTP_STREAM_CHANGE_EVENT:
			event_type = SCTP_PCB_FLAGS_STREAM_CHANGEEVNT;
			break;
		case SCTP_SEND_FAILED_EVENT:
			event_type = SCTP_PCB_FLAGS_RECVNSENDFAILEVNT;
			break;
		default:
			event_type = 0;
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		if (event_type > 0) {
			if (stcb) {
				event->se_on = sctp_stcb_is_feature_on(inp, stcb, event_type);
				SCTP_TCB_UNLOCK(stcb);
			} else {
				if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
				    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
				    (event->se_assoc_id == SCTP_FUTURE_ASSOC)) {
					SCTP_INP_RLOCK(inp);
					event->se_on = sctp_is_feature_on(inp, event_type);
					SCTP_INP_RUNLOCK(inp);
				} else {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					error = EINVAL;
				}
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_event);
		}
		break;
	}
	case SCTP_RECVRCVINFO:
	{
		int onoff;

		if (*optsize < sizeof(int)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
		} else {
			SCTP_INP_RLOCK(inp);
			onoff = sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVRCVINFO);
			SCTP_INP_RUNLOCK(inp);
		}
		if (error == 0) {
			/* return the option value */
			*(int *)optval = onoff;
			*optsize = sizeof(int);
		}
		break;
	}
	case SCTP_RECVNXTINFO:
	{
		int onoff;

		if (*optsize < sizeof(int)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
		} else {
			SCTP_INP_RLOCK(inp);
			onoff = sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVNXTINFO);
			SCTP_INP_RUNLOCK(inp);
		}
		if (error == 0) {
			/* return the option value */
			*(int *)optval = onoff;
			*optsize = sizeof(int);
		}
		break;
	}
	case SCTP_DEFAULT_SNDINFO:
	{
		struct sctp_sndinfo *info;

		SCTP_CHECK_AND_CAST(info, optval, struct sctp_sndinfo, *optsize);
		SCTP_FIND_STCB(inp, stcb, info->snd_assoc_id);

		if (stcb) {
			info->snd_sid = stcb->asoc.def_send.sinfo_stream;
			info->snd_flags = stcb->asoc.def_send.sinfo_flags;
			info->snd_flags &= 0xfff0;
			info->snd_ppid = stcb->asoc.def_send.sinfo_ppid;
			info->snd_context = stcb->asoc.def_send.sinfo_context;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (info->snd_assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				info->snd_sid = inp->def_send.sinfo_stream;
				info->snd_flags = inp->def_send.sinfo_flags;
				info->snd_flags &= 0xfff0;
				info->snd_ppid = inp->def_send.sinfo_ppid;
				info->snd_context = inp->def_send.sinfo_context;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_sndinfo);
		}
		break;
	}
	case SCTP_DEFAULT_PRINFO:
	{
		struct sctp_default_prinfo *info;

		SCTP_CHECK_AND_CAST(info, optval, struct sctp_default_prinfo, *optsize);
		SCTP_FIND_STCB(inp, stcb, info->pr_assoc_id);

		if (stcb) {
			info->pr_policy = PR_SCTP_POLICY(stcb->asoc.def_send.sinfo_flags);
			info->pr_value = stcb->asoc.def_send.sinfo_timetolive;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (info->pr_assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				info->pr_policy = PR_SCTP_POLICY(inp->def_send.sinfo_flags);
				info->pr_value = inp->def_send.sinfo_timetolive;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_default_prinfo);
		}
		break;
	}
	case SCTP_PEER_ADDR_THLDS:
	{
		struct sctp_paddrthlds *thlds;
		struct sctp_nets *net;

		SCTP_CHECK_AND_CAST(thlds, optval, struct sctp_paddrthlds, *optsize);
		SCTP_FIND_STCB(inp, stcb, thlds->spt_assoc_id);

		net = NULL;
		if (stcb) {
			net = sctp_findnet(stcb, (struct sockaddr *)&thlds->spt_address);
		} else {
			/* We increment here since sctp_findassociation_ep_addr() wil
			 * do a decrement if it finds the stcb as long as the locked
			 * tcb (last argument) is NOT a TCB.. aka NULL.
			 */
			SCTP_INP_INCR_REF(inp);
			stcb = sctp_findassociation_ep_addr(&inp, (struct sockaddr *)&thlds->spt_address, &net, NULL, NULL);
			if (stcb == NULL) {
				SCTP_INP_DECR_REF(inp);
			}
		}
		if (stcb && (net == NULL)) {
			struct sockaddr *sa;

			sa = (struct sockaddr *)&thlds->spt_address;
#ifdef INET
			if (sa->sa_family == AF_INET) {
				struct sockaddr_in *sin;

				sin = (struct sockaddr_in *)sa;
				if (sin->sin_addr.s_addr) {
					error = EINVAL;
					SCTP_TCB_UNLOCK(stcb);
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
					break;
				}
			} else
#endif
#ifdef INET6
			if (sa->sa_family == AF_INET6) {
				struct sockaddr_in6 *sin6;

				sin6 = (struct sockaddr_in6 *)sa;
				if (!IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
					error = EINVAL;
					SCTP_TCB_UNLOCK(stcb);
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
					break;
				}
			} else
#endif
#if defined(__Userspace__)
			if (sa->sa_family == AF_CONN) {
				struct sockaddr_conn *sconn;

				sconn = (struct sockaddr_conn *)sa;
				if (sconn->sconn_addr != NULL) {
					error = EINVAL;
					SCTP_TCB_UNLOCK(stcb);
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
					break;
				}
			} else
#endif
			{
				error = EAFNOSUPPORT;
				SCTP_TCB_UNLOCK(stcb);
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
				break;
			}
		}

		if (stcb) {
			if (net) {
				thlds->spt_pathmaxrxt = net->failure_threshold;
				thlds->spt_pathpfthld = net->pf_threshold;
			} else {
				thlds->spt_pathmaxrxt = stcb->asoc.def_net_failure;
				thlds->spt_pathpfthld = stcb->asoc.def_net_pf_threshold;
			}
			thlds->spt_assoc_id = sctp_get_associd(stcb);
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (thlds->spt_assoc_id == SCTP_FUTURE_ASSOC)) {
				/* Use endpoint defaults */
				SCTP_INP_RLOCK(inp);
				thlds->spt_pathmaxrxt = inp->sctp_ep.def_net_failure;
				thlds->spt_pathpfthld = inp->sctp_ep.def_net_pf_threshold;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_paddrthlds);
		}
		break;
	}
	case SCTP_REMOTE_UDP_ENCAPS_PORT:
	{
		struct sctp_udpencaps *encaps;
		struct sctp_nets *net;

		SCTP_CHECK_AND_CAST(encaps, optval, struct sctp_udpencaps, *optsize);
		SCTP_FIND_STCB(inp, stcb, encaps->sue_assoc_id);

		if (stcb) {
			net = sctp_findnet(stcb, (struct sockaddr *)&encaps->sue_address);
		} else {
			/* We increment here since sctp_findassociation_ep_addr() wil
			 * do a decrement if it finds the stcb as long as the locked
			 * tcb (last argument) is NOT a TCB.. aka NULL.
			 */
			net = NULL;
			SCTP_INP_INCR_REF(inp);
			stcb = sctp_findassociation_ep_addr(&inp, (struct sockaddr *)&encaps->sue_address, &net, NULL, NULL);
			if (stcb == NULL) {
				SCTP_INP_DECR_REF(inp);
			}
		}
		if (stcb && (net == NULL)) {
			struct sockaddr *sa;

			sa = (struct sockaddr *)&encaps->sue_address;
#ifdef INET
			if (sa->sa_family == AF_INET) {
				struct sockaddr_in *sin;

				sin = (struct sockaddr_in *)sa;
				if (sin->sin_addr.s_addr) {
					error = EINVAL;
					SCTP_TCB_UNLOCK(stcb);
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
					break;
				}
			} else
#endif
#ifdef INET6
			if (sa->sa_family == AF_INET6) {
				struct sockaddr_in6 *sin6;

				sin6 = (struct sockaddr_in6 *)sa;
				if (!IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
					error = EINVAL;
					SCTP_TCB_UNLOCK(stcb);
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
					break;
				}
			} else
#endif
#if defined(__Userspace__)
			if (sa->sa_family == AF_CONN) {
				struct sockaddr_conn *sconn;

				sconn = (struct sockaddr_conn *)sa;
				if (sconn->sconn_addr != NULL) {
					error = EINVAL;
					SCTP_TCB_UNLOCK(stcb);
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
					break;
				}
			} else
#endif
			{
				error = EAFNOSUPPORT;
				SCTP_TCB_UNLOCK(stcb);
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
				break;
			}
		}

		if (stcb) {
			if (net) {
				encaps->sue_port = net->port;
			} else {
				encaps->sue_port = stcb->asoc.port;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (encaps->sue_assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				encaps->sue_port = inp->sctp_ep.port;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_udpencaps);
		}
		break;
	}
	case SCTP_ENABLE_STREAM_RESET:
	{
		struct sctp_assoc_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, *optsize);
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);

		if (stcb) {
			av->assoc_value = (uint32_t)stcb->asoc.local_strreset_support;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				av->assoc_value = (uint32_t)inp->local_strreset_support;
				SCTP_INP_RUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		if (error == 0) {
			*optsize = sizeof(struct sctp_assoc_value);
		}
		break;
	}
	default:
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOPROTOOPT);
		error = ENOPROTOOPT;
		break;
	} /* end switch (sopt->sopt_name) */
	if (error) {
		*optsize = 0;
	}
	return (error);
}

#if defined(__Panda__) || defined(__Userspace__)
int
#else
static int
#endif
sctp_setopt(struct socket *so, int optname, void *optval, size_t optsize,
	    void *p)
{
	int error, set_opt;
	uint32_t *mopt;
	struct sctp_tcb *stcb = NULL;
	struct sctp_inpcb *inp = NULL;
	uint32_t vrf_id;

	if (optval == NULL) {
		SCTP_PRINTF("optval is NULL\n");
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (EINVAL);
	}
	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_PRINTF("inp is NULL?\n");
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (EINVAL);
	}
	vrf_id = inp->def_vrf_id;

	error = 0;
	switch (optname) {
	case SCTP_NODELAY:
	case SCTP_AUTOCLOSE:
	case SCTP_AUTO_ASCONF:
	case SCTP_EXPLICIT_EOR:
	case SCTP_DISABLE_FRAGMENTS:
	case SCTP_USE_EXT_RCVINFO:
	case SCTP_I_WANT_MAPPED_V4_ADDR:
		/* copy in the option value */
		SCTP_CHECK_AND_CAST(mopt, optval, uint32_t, optsize);
		set_opt = 0;
		if (error)
			break;
		switch (optname) {
		case SCTP_DISABLE_FRAGMENTS:
			set_opt = SCTP_PCB_FLAGS_NO_FRAGMENT;
			break;
		case SCTP_AUTO_ASCONF:
			/*
			 * NOTE: we don't really support this flag
			 */
			if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
				/* only valid for bound all sockets */
				if ((SCTP_BASE_SYSCTL(sctp_auto_asconf) == 0) &&
				    (*mopt != 0)) {
					/* forbidden by admin */
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EPERM);
					return (EPERM);
				}
				set_opt = SCTP_PCB_FLAGS_AUTO_ASCONF;
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				return (EINVAL);
			}
			break;
		case SCTP_EXPLICIT_EOR:
			set_opt = SCTP_PCB_FLAGS_EXPLICIT_EOR;
			break;
		case SCTP_USE_EXT_RCVINFO:
			set_opt = SCTP_PCB_FLAGS_EXT_RCVINFO;
			break;
		case SCTP_I_WANT_MAPPED_V4_ADDR:
			if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
				set_opt = SCTP_PCB_FLAGS_NEEDS_MAPPED_V4;
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				return (EINVAL);
			}
			break;
		case SCTP_NODELAY:
			set_opt = SCTP_PCB_FLAGS_NODELAY;
			break;
		case SCTP_AUTOCLOSE:
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				return (EINVAL);
			}
			set_opt = SCTP_PCB_FLAGS_AUTOCLOSE;
			/*
			 * The value is in ticks. Note this does not effect
			 * old associations, only new ones.
			 */
			inp->sctp_ep.auto_close_time = SEC_TO_TICKS(*mopt);
			break;
		}
		SCTP_INP_WLOCK(inp);
		if (*mopt != 0) {
			sctp_feature_on(inp, set_opt);
		} else {
			sctp_feature_off(inp, set_opt);
		}
		SCTP_INP_WUNLOCK(inp);
		break;
	case SCTP_REUSE_PORT:
	{
		SCTP_CHECK_AND_CAST(mopt, optval, uint32_t, optsize);
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND)  == 0) {
			/* Can't set it after we are bound */
			error = EINVAL;
			break;
		}
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_UDPTYPE)) {
			/* Can't do this for a 1-m socket */
			error = EINVAL;
			break;
		}
		if (optval)
			sctp_feature_on(inp, SCTP_PCB_FLAGS_PORTREUSE);
		else
			sctp_feature_off(inp, SCTP_PCB_FLAGS_PORTREUSE);
		break;
	}
	case SCTP_PARTIAL_DELIVERY_POINT:
	{
		uint32_t *value;

		SCTP_CHECK_AND_CAST(value, optval, uint32_t, optsize);
		if (*value > SCTP_SB_LIMIT_RCV(so)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		inp->partial_delivery_point = *value;
		break;
	}
	case SCTP_FRAGMENT_INTERLEAVE:
		/* not yet until we re-write sctp_recvmsg() */
	{
		uint32_t *level;

		SCTP_CHECK_AND_CAST(level, optval, uint32_t, optsize);
		if (*level == SCTP_FRAG_LEVEL_2) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_FRAG_INTERLEAVE);
			sctp_feature_on(inp, SCTP_PCB_FLAGS_INTERLEAVE_STRMS);
		} else if (*level == SCTP_FRAG_LEVEL_1) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_FRAG_INTERLEAVE);
			sctp_feature_off(inp, SCTP_PCB_FLAGS_INTERLEAVE_STRMS);
		} else if (*level == SCTP_FRAG_LEVEL_0) {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_FRAG_INTERLEAVE);
			sctp_feature_off(inp, SCTP_PCB_FLAGS_INTERLEAVE_STRMS);

		} else {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
		}
		break;
	}
	case SCTP_NDATA_ENABLE:
		SCTP_CHECK_AND_CAST(mopt, optval, uint32_t, optsize);
		if (*mopt) {
			/* Turn it on */
			if ((sctp_is_feature_on(inp, SCTP_PCB_FLAGS_FRAG_INTERLEAVE))  &&
			    (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_INTERLEAVE_STRMS))) {
				sctp_feature_on(inp, SCTP_PCB_FLAGS_USE_NDATA);
			} else {
				/* Must have Frag interleave and stream interleave on */
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		} else {
			/* Turn it off */
			sctp_feature_off(inp, SCTP_PCB_FLAGS_USE_NDATA);
		}
		break;
	case SCTP_CMT_ON_OFF:
		if (SCTP_BASE_SYSCTL(sctp_cmt_on_off)) {
			struct sctp_assoc_value *av;

			SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, optsize);
			if (av->assoc_value > SCTP_CMT_MAX) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
				break;
			}
			SCTP_FIND_STCB(inp, stcb, av->assoc_id);
			if (stcb) {
				stcb->asoc.sctp_cmt_on_off = av->assoc_value;
				SCTP_TCB_UNLOCK(stcb);
			} else {
				if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
				    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
				    (av->assoc_id == SCTP_FUTURE_ASSOC) ||
				    (av->assoc_id == SCTP_ALL_ASSOC)) {
					SCTP_INP_WLOCK(inp);
					inp->sctp_cmt_on_off = av->assoc_value;
					SCTP_INP_WUNLOCK(inp);
				}
				if ((av->assoc_id == SCTP_CURRENT_ASSOC) ||
				    (av->assoc_id == SCTP_ALL_ASSOC)) {
					SCTP_INP_RLOCK(inp);
					LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
						SCTP_TCB_LOCK(stcb);
						stcb->asoc.sctp_cmt_on_off = av->assoc_value;
						SCTP_TCB_UNLOCK(stcb);
					}
					SCTP_INP_RUNLOCK(inp);
				}
			}
		} else {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOPROTOOPT);
			error = ENOPROTOOPT;
		}
		break;
	case SCTP_PLUGGABLE_CC:
	{
		struct sctp_assoc_value *av;
		struct sctp_nets *net;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, optsize);
		if ((av->assoc_value != SCTP_CC_RFC2581) &&
		    (av->assoc_value != SCTP_CC_HSTCP) &&
		    (av->assoc_value != SCTP_CC_HTCP) &&
		    (av->assoc_value != SCTP_CC_RTCC)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);
		if (stcb) {
			stcb->asoc.cc_functions = sctp_cc_functions[av->assoc_value];
			stcb->asoc.congestion_control_module = av->assoc_value;
			if (stcb->asoc.cc_functions.sctp_set_initial_cc_param != NULL) {
				TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
					stcb->asoc.cc_functions.sctp_set_initial_cc_param(stcb, net);
				}
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC) ||
			    (av->assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				inp->sctp_ep.sctp_default_cc_module = av->assoc_value;
				SCTP_INP_WUNLOCK(inp);
			}
			if ((av->assoc_id == SCTP_CURRENT_ASSOC) ||
			    (av->assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					stcb->asoc.cc_functions = sctp_cc_functions[av->assoc_value];
					stcb->asoc.congestion_control_module = av->assoc_value;
					if (stcb->asoc.cc_functions.sctp_set_initial_cc_param != NULL) {
						TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
							stcb->asoc.cc_functions.sctp_set_initial_cc_param(stcb, net);
						}
					}
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}
		}
		break;
	}
	case SCTP_CC_OPTION:
	{
		struct sctp_cc_option *cc_opt;

		SCTP_CHECK_AND_CAST(cc_opt, optval, struct sctp_cc_option, optsize);
		SCTP_FIND_STCB(inp, stcb, cc_opt->aid_value.assoc_id);
		if (stcb == NULL) {
			if (cc_opt->aid_value.assoc_id == SCTP_CURRENT_ASSOC) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					if (stcb->asoc.cc_functions.sctp_cwnd_socket_option) {
						(*stcb->asoc.cc_functions.sctp_cwnd_socket_option)(stcb, 1, cc_opt);
					}
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			} else {
				error = EINVAL;
			}
		} else {
			if (stcb->asoc.cc_functions.sctp_cwnd_socket_option == NULL) {
				error = ENOTSUP;
			} else {
				error = (*stcb->asoc.cc_functions.sctp_cwnd_socket_option)(stcb, 1,
											   cc_opt);
			}
			SCTP_TCB_UNLOCK(stcb);
		}
		break;
	}
	case SCTP_PLUGGABLE_SS:
	{
		struct sctp_assoc_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, optsize);
		if ((av->assoc_value != SCTP_SS_DEFAULT) &&
		    (av->assoc_value != SCTP_SS_ROUND_ROBIN) &&
		    (av->assoc_value != SCTP_SS_ROUND_ROBIN_PACKET) &&
		    (av->assoc_value != SCTP_SS_PRIORITY) &&
		    (av->assoc_value != SCTP_SS_FAIR_BANDWITH) &&
		    (av->assoc_value != SCTP_SS_FIRST_COME)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);
		if (stcb) {
			stcb->asoc.ss_functions.sctp_ss_clear(stcb, &stcb->asoc, 1, 1);
			stcb->asoc.ss_functions = sctp_ss_functions[av->assoc_value];
			stcb->asoc.stream_scheduling_module = av->assoc_value;
			stcb->asoc.ss_functions.sctp_ss_init(stcb, &stcb->asoc, 1);
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC) ||
			    (av->assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				inp->sctp_ep.sctp_default_ss_module = av->assoc_value;
				SCTP_INP_WUNLOCK(inp);
			}
			if ((av->assoc_id == SCTP_CURRENT_ASSOC) ||
			    (av->assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					stcb->asoc.ss_functions.sctp_ss_clear(stcb, &stcb->asoc, 1, 1);
					stcb->asoc.ss_functions = sctp_ss_functions[av->assoc_value];
					stcb->asoc.stream_scheduling_module = av->assoc_value;
					stcb->asoc.ss_functions.sctp_ss_init(stcb, &stcb->asoc, 1);
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}
		}
		break;
	}
	case SCTP_SS_VALUE:
	{
		struct sctp_stream_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_stream_value, optsize);
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);
		if (stcb) {
			if (stcb->asoc.ss_functions.sctp_ss_set_value(stcb, &stcb->asoc, &stcb->asoc.strmout[av->stream_id],
			                                              av->stream_value) < 0) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if (av->assoc_id == SCTP_CURRENT_ASSOC) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					stcb->asoc.ss_functions.sctp_ss_set_value(stcb,
					                                          &stcb->asoc,
					                                          &stcb->asoc.strmout[av->stream_id],
					                                          av->stream_value);
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);

			} else {
				/* Can't set stream value without association */
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		break;
	}
	case SCTP_CLR_STAT_LOG:
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EOPNOTSUPP);
		error = EOPNOTSUPP;
		break;
	case SCTP_CONTEXT:
	{
		struct sctp_assoc_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, optsize);
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);

		if (stcb) {
			stcb->asoc.context = av->assoc_value;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC) ||
			    (av->assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				inp->sctp_context = av->assoc_value;
				SCTP_INP_WUNLOCK(inp);
			}
			if ((av->assoc_id == SCTP_CURRENT_ASSOC) ||
			    (av->assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					stcb->asoc.context = av->assoc_value;
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}
		}
		break;
	}
	case SCTP_VRF_ID:
	{
		uint32_t *default_vrfid;
#ifdef SCTP_MVRF
		int i;
#endif
		SCTP_CHECK_AND_CAST(default_vrfid, optval, uint32_t, optsize);
		if (*default_vrfid > SCTP_MAX_VRF_ID) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
#ifdef SCTP_MVRF
		for (i = 0; i < inp->num_vrfs; i++) {
			/* The VRF must be in the VRF list */
			if (*default_vrfid == inp->m_vrf_ids[i]) {
				SCTP_INP_WLOCK(inp);
 				inp->def_vrf_id = *default_vrfid;
				SCTP_INP_WUNLOCK(inp);
				goto sctp_done;
			}
		}
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		error = EINVAL;
#else
		inp->def_vrf_id = *default_vrfid;
#endif
#ifdef SCTP_MVRF
	sctp_done:
#endif
		break;
	}
	case SCTP_DEL_VRF_ID:
	{
#ifdef SCTP_MVRF
		uint32_t *del_vrfid;
		int i, fnd = 0;

		SCTP_CHECK_AND_CAST(del_vrfid, optval, uint32_t, optsize);
		if (*del_vrfid > SCTP_MAX_VRF_ID) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		if (inp->num_vrfs == 1) {
			/* Can't delete last one */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND) == 0) {
			/* Can't add more once you are bound */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		SCTP_INP_WLOCK(inp);
		for (i = 0; i < inp->num_vrfs; i++) {
			if (*del_vrfid == inp->m_vrf_ids[i]) {
				fnd = 1;
				break;
			}
		}
		if (!fnd) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		if (i != (inp->num_vrfs - 1)) {
			/* Take bottom one and move to this slot */
			inp->m_vrf_ids[i] = inp->m_vrf_ids[(inp->num_vrfs-1)];
		}
		if (*del_vrfid == inp->def_vrf_id) {
			/* Take the first one as the new default */
			inp->def_vrf_id = inp->m_vrf_ids[0];
		}
		/* Drop the number by one killing last one */
		inp->num_vrfs--;
#else
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EOPNOTSUPP);
		error = EOPNOTSUPP;
#endif
		break;
	}
	case SCTP_ADD_VRF_ID:
	{
#ifdef SCTP_MVRF
		uint32_t *add_vrfid;
		int i;

		SCTP_CHECK_AND_CAST(add_vrfid, optval, uint32_t, optsize);
		if (*add_vrfid > SCTP_MAX_VRF_ID) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND) == 0) {
			/* Can't add more once you are bound */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		SCTP_INP_WLOCK(inp);
		/* Verify its not already here */
		for (i = 0; i < inp->num_vrfs; i++) {
			if (*add_vrfid == inp->m_vrf_ids[i]) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EALREADY);
				error = EALREADY;
				SCTP_INP_WUNLOCK(inp);
				break;
			}
		}
		if ((inp->num_vrfs + 1) > inp->vrf_size) {
			/* need to grow array */
			uint32_t *tarray;
			SCTP_MALLOC(tarray, uint32_t *,
				    (sizeof(uint32_t) * (inp->vrf_size + SCTP_DEFAULT_VRF_SIZE)),
				    SCTP_M_MVRF);
			if (tarray == NULL) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOMEM);
				error = ENOMEM;
				SCTP_INP_WUNLOCK(inp);
				break;
			}
			memcpy(tarray, inp->m_vrf_ids, (sizeof(uint32_t) * inp->vrf_size));
			SCTP_FREE(inp->m_vrf_ids, SCTP_M_MVRF);
			inp->m_vrf_ids = tarray;
			inp->vrf_size += SCTP_DEFAULT_VRF_SIZE;
		}
		inp->m_vrf_ids[inp->num_vrfs] = *add_vrfid;
		inp->num_vrfs++;
		SCTP_INP_WUNLOCK(inp);
#else
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EOPNOTSUPP);
		error = EOPNOTSUPP;
#endif
	 	break;
	}
	case SCTP_DELAYED_SACK:
	{
		struct sctp_sack_info *sack;

		SCTP_CHECK_AND_CAST(sack, optval, struct sctp_sack_info, optsize);
		SCTP_FIND_STCB(inp, stcb, sack->sack_assoc_id);
		if (sack->sack_delay) {
			if (sack->sack_delay > SCTP_MAX_SACK_DELAY)
				sack->sack_delay = SCTP_MAX_SACK_DELAY;
			if (MSEC_TO_TICKS(sack->sack_delay) < 1) {
				sack->sack_delay = TICKS_TO_MSEC(1);
			}
		}
		if (stcb) {
			if (sack->sack_delay) {
				stcb->asoc.delayed_ack = sack->sack_delay;
			}
			if (sack->sack_freq) {
				stcb->asoc.sack_freq = sack->sack_freq;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (sack->sack_assoc_id == SCTP_FUTURE_ASSOC) ||
			    (sack->sack_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				if (sack->sack_delay) {
					inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_RECV] = MSEC_TO_TICKS(sack->sack_delay);
				}
				if (sack->sack_freq) {
					inp->sctp_ep.sctp_sack_freq = sack->sack_freq;
				}
				SCTP_INP_WUNLOCK(inp);
			}
			if ((sack->sack_assoc_id == SCTP_CURRENT_ASSOC) ||
			    (sack->sack_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					if (sack->sack_delay) {
						stcb->asoc.delayed_ack = sack->sack_delay;
					}
					if (sack->sack_freq) {
						stcb->asoc.sack_freq = sack->sack_freq;
					}
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}
		}
		break;
	}
	case SCTP_AUTH_CHUNK:
	{
		struct sctp_authchunk *sauth;

		SCTP_CHECK_AND_CAST(sauth, optval, struct sctp_authchunk, optsize);

		SCTP_INP_WLOCK(inp);
		if (sctp_auth_add_chunk(sauth->sauth_chunk, inp->sctp_ep.local_auth_chunks)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
		}
		SCTP_INP_WUNLOCK(inp);
		break;
	}
	case SCTP_AUTH_KEY:
	{
		struct sctp_authkey *sca;
		struct sctp_keyhead *shared_keys;
		sctp_sharedkey_t *shared_key;
		sctp_key_t *key = NULL;
		size_t size;

		SCTP_CHECK_AND_CAST(sca, optval, struct sctp_authkey, optsize);
		if (sca->sca_keylength == 0) {
			size = optsize - sizeof(struct sctp_authkey);
		} else {
		        if (sca->sca_keylength + sizeof(struct sctp_authkey) <= optsize) {
				size = sca->sca_keylength;
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
				break;
			}
		}
		SCTP_FIND_STCB(inp, stcb, sca->sca_assoc_id);

		if (stcb) {
			shared_keys = &stcb->asoc.shared_keys;
			/* clear the cached keys for this key id */
			sctp_clear_cachedkeys(stcb, sca->sca_keynumber);
			/*
			 * create the new shared key and
			 * insert/replace it
			 */
			if (size > 0) {
				key = sctp_set_key(sca->sca_key, (uint32_t) size);
				if (key == NULL) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOMEM);
					error = ENOMEM;
					SCTP_TCB_UNLOCK(stcb);
					break;
				}
			}
			shared_key = sctp_alloc_sharedkey();
			if (shared_key == NULL) {
				sctp_free_key(key);
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOMEM);
				error = ENOMEM;
				SCTP_TCB_UNLOCK(stcb);
				break;
			}
			shared_key->key = key;
			shared_key->keyid = sca->sca_keynumber;
			error = sctp_insert_sharedkey(shared_keys, shared_key);
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (sca->sca_assoc_id == SCTP_FUTURE_ASSOC) ||
			    (sca->sca_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				shared_keys = &inp->sctp_ep.shared_keys;
				/*
				 * clear the cached keys on all assocs for
				 * this key id
				 */
				sctp_clear_cachedkeys_ep(inp, sca->sca_keynumber);
				/*
				 * create the new shared key and
				 * insert/replace it
				 */
				if (size > 0) {
					key = sctp_set_key(sca->sca_key, (uint32_t) size);
					if (key == NULL) {
						SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOMEM);
						error = ENOMEM;
						SCTP_INP_WUNLOCK(inp);
						break;
					}
				}
				shared_key = sctp_alloc_sharedkey();
				if (shared_key == NULL) {
					sctp_free_key(key);
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOMEM);
					error = ENOMEM;
					SCTP_INP_WUNLOCK(inp);
					break;
				}
				shared_key->key = key;
				shared_key->keyid = sca->sca_keynumber;
				error = sctp_insert_sharedkey(shared_keys, shared_key);
				SCTP_INP_WUNLOCK(inp);
			}
			if ((sca->sca_assoc_id == SCTP_CURRENT_ASSOC) ||
			    (sca->sca_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					shared_keys = &stcb->asoc.shared_keys;
					/* clear the cached keys for this key id */
					sctp_clear_cachedkeys(stcb, sca->sca_keynumber);
					/*
					 * create the new shared key and
					 * insert/replace it
					 */
					if (size > 0) {
						key = sctp_set_key(sca->sca_key, (uint32_t) size);
						if (key == NULL) {
							SCTP_TCB_UNLOCK(stcb);
							continue;
						}
					}
					shared_key = sctp_alloc_sharedkey();
					if (shared_key == NULL) {
						sctp_free_key(key);
						SCTP_TCB_UNLOCK(stcb);
						continue;
					}
					shared_key->key = key;
					shared_key->keyid = sca->sca_keynumber;
					error = sctp_insert_sharedkey(shared_keys, shared_key);
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}
		}
		break;
	}
	case SCTP_HMAC_IDENT:
	{
		struct sctp_hmacalgo *shmac;
		sctp_hmaclist_t *hmaclist;
		uint16_t hmacid;
		uint32_t i;

		SCTP_CHECK_AND_CAST(shmac, optval, struct sctp_hmacalgo, optsize);
		if (optsize < sizeof(struct sctp_hmacalgo) + shmac->shmac_number_of_idents * sizeof(uint16_t)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}

		hmaclist = sctp_alloc_hmaclist(shmac->shmac_number_of_idents);
		if (hmaclist == NULL) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOMEM);
			error = ENOMEM;
			break;
		}
		for (i = 0; i < shmac->shmac_number_of_idents; i++) {
			hmacid = shmac->shmac_idents[i];
			if (sctp_auth_add_hmacid(hmaclist, hmacid)) {
				/* invalid HMACs were found */;
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
				sctp_free_hmaclist(hmaclist);
				goto sctp_set_hmac_done;
			}
		}
		for (i = 0; i < hmaclist->num_algo; i++) {
			if (hmaclist->hmac[i] == SCTP_AUTH_HMAC_ID_SHA1) {
				/* already in list */
				break;
			}
		}
		if (i == hmaclist->num_algo) {
			/* not found in list */
			sctp_free_hmaclist(hmaclist);
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		/* set it on the endpoint */
		SCTP_INP_WLOCK(inp);
		if (inp->sctp_ep.local_hmacs)
			sctp_free_hmaclist(inp->sctp_ep.local_hmacs);
		inp->sctp_ep.local_hmacs = hmaclist;
		SCTP_INP_WUNLOCK(inp);
	sctp_set_hmac_done:
		break;
	}
	case SCTP_AUTH_ACTIVE_KEY:
	{
		struct sctp_authkeyid *scact;

		SCTP_CHECK_AND_CAST(scact, optval, struct sctp_authkeyid, optsize);
		SCTP_FIND_STCB(inp, stcb, scact->scact_assoc_id);

		/* set the active key on the right place */
		if (stcb) {
			/* set the active key on the assoc */
			if (sctp_auth_setactivekey(stcb,
						   scact->scact_keynumber)) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL,
						    SCTP_FROM_SCTP_USRREQ,
						    EINVAL);
				error = EINVAL;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (scact->scact_assoc_id == SCTP_FUTURE_ASSOC) ||
			    (scact->scact_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				if (sctp_auth_setactivekey_ep(inp, scact->scact_keynumber)) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					error = EINVAL;
				}
				SCTP_INP_WUNLOCK(inp);
			}
			if ((scact->scact_assoc_id == SCTP_CURRENT_ASSOC) ||
			    (scact->scact_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					sctp_auth_setactivekey(stcb, scact->scact_keynumber);
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}
		}
		break;
	}
	case SCTP_AUTH_DELETE_KEY:
	{
		struct sctp_authkeyid *scdel;

		SCTP_CHECK_AND_CAST(scdel, optval, struct sctp_authkeyid, optsize);
		SCTP_FIND_STCB(inp, stcb, scdel->scact_assoc_id);

		/* delete the key from the right place */
		if (stcb) {
			if (sctp_delete_sharedkey(stcb, scdel->scact_keynumber)) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (scdel->scact_assoc_id == SCTP_FUTURE_ASSOC) ||
			    (scdel->scact_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				if (sctp_delete_sharedkey_ep(inp, scdel->scact_keynumber)) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					error = EINVAL;
				}
				SCTP_INP_WUNLOCK(inp);
			}
			if ((scdel->scact_assoc_id == SCTP_CURRENT_ASSOC) ||
			    (scdel->scact_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					sctp_delete_sharedkey(stcb, scdel->scact_keynumber);
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}
		}
		break;
	}
	case SCTP_AUTH_DEACTIVATE_KEY:
	{
		struct sctp_authkeyid *keyid;

		SCTP_CHECK_AND_CAST(keyid, optval, struct sctp_authkeyid, optsize);
		SCTP_FIND_STCB(inp, stcb, keyid->scact_assoc_id);

		/* deactivate the key from the right place */
		if (stcb) {
			if (sctp_deact_sharedkey(stcb, keyid->scact_keynumber)) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (keyid->scact_assoc_id == SCTP_FUTURE_ASSOC) ||
			    (keyid->scact_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				if (sctp_deact_sharedkey_ep(inp, keyid->scact_keynumber)) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					error = EINVAL;
				}
				SCTP_INP_WUNLOCK(inp);
			}
			if ((keyid->scact_assoc_id == SCTP_CURRENT_ASSOC) ||
			    (keyid->scact_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					sctp_deact_sharedkey(stcb, keyid->scact_keynumber);
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}
		}
		break;
	}
	case SCTP_ENABLE_STREAM_RESET:
	{
		struct sctp_assoc_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, optsize);
		if (av->assoc_value & (~SCTP_ENABLE_VALUE_MASK)) {
		        SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);
		if (stcb) {
			stcb->asoc.local_strreset_support = (uint8_t)av->assoc_value;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC) ||
			    (av->assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				inp->local_strreset_support = (uint8_t)av->assoc_value;
				SCTP_INP_WUNLOCK(inp);
			}
			if ((av->assoc_id == SCTP_CURRENT_ASSOC) ||
			    (av->assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					stcb->asoc.local_strreset_support = (uint8_t)av->assoc_value;
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}

		}
		break;
	}
	case SCTP_RESET_STREAMS:
	{
		struct sctp_reset_streams *strrst;
		int i, send_out = 0;
		int send_in = 0;

		SCTP_CHECK_AND_CAST(strrst, optval, struct sctp_reset_streams, optsize);
		SCTP_FIND_STCB(inp, stcb, strrst->srs_assoc_id);
		if (stcb == NULL) {
		        SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOENT);
			error = ENOENT;
			break;
		}
		if (stcb->asoc.peer_supports_strreset == 0) {
			/*
			 * Peer does not support the chunk type.
			 */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EOPNOTSUPP);
			error = EOPNOTSUPP;
			SCTP_TCB_UNLOCK(stcb);
			break;
		}
		if (stcb->asoc.stream_reset_outstanding) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EALREADY);
			error = EALREADY;
			SCTP_TCB_UNLOCK(stcb);
			break;
		}
		if (strrst->srs_flags & SCTP_STREAM_RESET_INCOMING) {
			send_in = 1;
		}
		if (strrst->srs_flags & SCTP_STREAM_RESET_OUTGOING) {
			send_out = 1;
		}
		if ((send_in == 0) && (send_out == 0)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			SCTP_TCB_UNLOCK(stcb);
			break;
		}
		for (i = 0; i < strrst->srs_number_streams; i++) {
			if ((send_in) &&
			    (strrst->srs_stream_list[i] > stcb->asoc.streamincnt)) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
				break;
			}
			if ((send_out) &&
			    (strrst->srs_stream_list[i] > stcb->asoc.streamoutcnt)) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
				break;
			}
		}
		if (error) {
			SCTP_TCB_UNLOCK(stcb);
			break;
		}
		error = sctp_send_str_reset_req(stcb, strrst->srs_number_streams,
						strrst->srs_stream_list,
						send_out, send_in, 0, 0, 0, 0, 0);

		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_STRRST_REQ, SCTP_SO_LOCKED);
		SCTP_TCB_UNLOCK(stcb);
		break;
	}
	case SCTP_ADD_STREAMS:
	{
		struct sctp_add_streams *stradd;
		uint8_t addstream = 0;
		uint16_t add_o_strmcnt = 0;
		uint16_t add_i_strmcnt = 0;

		SCTP_CHECK_AND_CAST(stradd, optval, struct sctp_add_streams, optsize);
		SCTP_FIND_STCB(inp, stcb, stradd->sas_assoc_id);
		if (stcb == NULL) {
		        SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOENT);
			error = ENOENT;
			break;
		}
		if (stcb->asoc.peer_supports_strreset == 0) {
			/*
			 * Peer does not support the chunk type.
			 */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EOPNOTSUPP);
			error = EOPNOTSUPP;
			SCTP_TCB_UNLOCK(stcb);
			break;
		}
		if (stcb->asoc.stream_reset_outstanding) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EALREADY);
			error = EALREADY;
			SCTP_TCB_UNLOCK(stcb);
			break;
		}
		if ((stradd->sas_outstrms == 0) &&
		    (stradd->sas_instrms == 0)) {
			error = EINVAL;
			goto skip_stuff;
		}
		if (stradd->sas_outstrms) {
			addstream = 1;
			/* We allocate here */
			add_o_strmcnt = stradd->sas_outstrms;
			if ((((int)add_o_strmcnt) + ((int)stcb->asoc.streamoutcnt)) > 0x0000ffff) {
				/* You can't have more than 64k */
				error = EINVAL;
				goto skip_stuff;
			}
		}
		if (stradd->sas_instrms) {
			int cnt;

			addstream |= 2;
			/* We allocate inside sctp_send_str_reset_req() */
			add_i_strmcnt = stradd->sas_instrms;
			cnt = add_i_strmcnt;
			cnt += stcb->asoc.streamincnt;
			if (cnt > 0x0000ffff) {
				/* You can't have more than 64k */
				error = EINVAL;
				goto skip_stuff;
			}
			if (cnt > (int)stcb->asoc.max_inbound_streams) {
				/* More than you are allowed */
				error = EINVAL;
				goto skip_stuff;
			}
		}
		error = sctp_send_str_reset_req(stcb, 0, NULL, 0, 0, 0, addstream, add_o_strmcnt, add_i_strmcnt, 0);
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_STRRST_REQ, SCTP_SO_LOCKED);
	skip_stuff:
		SCTP_TCB_UNLOCK(stcb);
		break;
	}
	case SCTP_RESET_ASSOC:
	{
		uint32_t *value;

		SCTP_CHECK_AND_CAST(value, optval, uint32_t, optsize);
		SCTP_FIND_STCB(inp, stcb, (sctp_assoc_t) *value);
		if (stcb == NULL) {
		        SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOENT);
			error = ENOENT;
			break;
		}
		if (stcb->asoc.peer_supports_strreset == 0) {
			/*
			 * Peer does not support the chunk type.
			 */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EOPNOTSUPP);
			error = EOPNOTSUPP;
			SCTP_TCB_UNLOCK(stcb);
			break;
		}
		if (stcb->asoc.stream_reset_outstanding) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EALREADY);
			error = EALREADY;
			SCTP_TCB_UNLOCK(stcb);
			break;
		}
		error = sctp_send_str_reset_req(stcb, 0, NULL, 0, 0, 1, 0, 0, 0, 0);
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_STRRST_REQ, SCTP_SO_LOCKED);
		SCTP_TCB_UNLOCK(stcb);
		break;
	}
	case SCTP_CONNECT_X:
		if (optsize < (sizeof(int) + sizeof(struct sockaddr_in))) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		error = sctp_do_connect_x(so, inp, optval, optsize, p, 0);
		break;
	case SCTP_CONNECT_X_DELAYED:
		if (optsize < (sizeof(int) + sizeof(struct sockaddr_in))) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		error = sctp_do_connect_x(so, inp, optval, optsize, p, 1);
		break;
	case SCTP_CONNECT_X_COMPLETE:
	{
		struct sockaddr *sa;
		struct sctp_nets *net;

		/* FIXME MT: check correct? */
		SCTP_CHECK_AND_CAST(sa, optval, struct sockaddr, optsize);

		/* find tcb */
		if (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) {
			SCTP_INP_RLOCK(inp);
			stcb = LIST_FIRST(&inp->sctp_asoc_list);
			if (stcb) {
				SCTP_TCB_LOCK(stcb);
				net = sctp_findnet(stcb, sa);
			}
			SCTP_INP_RUNLOCK(inp);
		} else {
			/* We increment here since sctp_findassociation_ep_addr() wil
			 * do a decrement if it finds the stcb as long as the locked
			 * tcb (last argument) is NOT a TCB.. aka NULL.
			 */
			SCTP_INP_INCR_REF(inp);
			stcb = sctp_findassociation_ep_addr(&inp, sa, &net, NULL, NULL);
			if (stcb == NULL) {
				SCTP_INP_DECR_REF(inp);
			}
		}

		if (stcb == NULL) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOENT);
			error = ENOENT;
			break;
		}
		if (stcb->asoc.delayed_connection == 1) {
			stcb->asoc.delayed_connection = 0;
			(void)SCTP_GETTIME_TIMEVAL(&stcb->asoc.time_entered);
			sctp_timer_stop(SCTP_TIMER_TYPE_INIT, inp, stcb,
					stcb->asoc.primary_destination,
					SCTP_FROM_SCTP_USRREQ+SCTP_LOC_9);
			sctp_send_initiate(inp, stcb, SCTP_SO_LOCKED);
		} else {
			/*
			 * already expired or did not use delayed
			 * connectx
			 */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EALREADY);
			error = EALREADY;
		}
		SCTP_TCB_UNLOCK(stcb);
		break;
	}
	case SCTP_MAX_BURST:
	{
#if defined(__FreeBSD__) && __FreeBSD_version < 900000
		uint8_t *burst;

		SCTP_CHECK_AND_CAST(burst, optval, uint8_t, optsize);

		SCTP_INP_WLOCK(inp);
		inp->sctp_ep.max_burst = *burst;
		SCTP_INP_WUNLOCK(inp);
#else
		struct sctp_assoc_value *av;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, optsize);
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);

		if (stcb) {
			stcb->asoc.max_burst = av->assoc_value;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC) ||
			    (av->assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				inp->sctp_ep.max_burst = av->assoc_value;
				SCTP_INP_WUNLOCK(inp);
			}
			if ((av->assoc_id == SCTP_CURRENT_ASSOC) ||
			    (av->assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					stcb->asoc.max_burst = av->assoc_value;
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}
		}
#endif
		break;
	}
	case SCTP_MAXSEG:
	{
		struct sctp_assoc_value *av;
		int ovh;

		SCTP_CHECK_AND_CAST(av, optval, struct sctp_assoc_value, optsize);
		SCTP_FIND_STCB(inp, stcb, av->assoc_id);

		if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
			ovh = SCTP_MED_OVERHEAD;
		} else {
			ovh = SCTP_MED_V4_OVERHEAD;
		}
		if (stcb) {
			if (av->assoc_value) {
				stcb->asoc.sctp_frag_point = (av->assoc_value + ovh);
			} else {
				stcb->asoc.sctp_frag_point = SCTP_DEFAULT_MAXSEGMENT;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (av->assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				/* FIXME MT: I think this is not in tune with the API ID */
				if (av->assoc_value) {
					inp->sctp_frag_point = (av->assoc_value + ovh);
				} else {
					inp->sctp_frag_point = SCTP_DEFAULT_MAXSEGMENT;
				}
				SCTP_INP_WUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		break;
	}
	case SCTP_EVENTS:
	{
		struct sctp_event_subscribe *events;

		SCTP_CHECK_AND_CAST(events, optval, struct sctp_event_subscribe, optsize);

		SCTP_INP_WLOCK(inp);
		if (events->sctp_data_io_event) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_RECVDATAIOEVNT);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_RECVDATAIOEVNT);
		}

		if (events->sctp_association_event) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_RECVASSOCEVNT);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_RECVASSOCEVNT);
		}

		if (events->sctp_address_event) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_RECVPADDREVNT);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_RECVPADDREVNT);
		}

		if (events->sctp_send_failure_event) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_RECVSENDFAILEVNT);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_RECVSENDFAILEVNT);
		}

		if (events->sctp_peer_error_event) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_RECVPEERERR);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_RECVPEERERR);
		}

		if (events->sctp_shutdown_event) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_RECVSHUTDOWNEVNT);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_RECVSHUTDOWNEVNT);
		}

		if (events->sctp_partial_delivery_event) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_PDAPIEVNT);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_PDAPIEVNT);
		}

		if (events->sctp_adaptation_layer_event) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_ADAPTATIONEVNT);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_ADAPTATIONEVNT);
		}

		if (events->sctp_authentication_event) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_AUTHEVNT);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_AUTHEVNT);
		}

		if (events->sctp_sender_dry_event) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_DRYEVNT);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_DRYEVNT);
		}

		if (events->sctp_stream_reset_event) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_STREAM_RESETEVNT);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_STREAM_RESETEVNT);
		}
		SCTP_INP_WUNLOCK(inp);

		SCTP_INP_RLOCK(inp);
		LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
			SCTP_TCB_LOCK(stcb);
			if (events->sctp_association_event) {
				sctp_stcb_feature_on(inp, stcb, SCTP_PCB_FLAGS_RECVASSOCEVNT);
			} else {
				sctp_stcb_feature_off(inp, stcb, SCTP_PCB_FLAGS_RECVASSOCEVNT);
			}
			if (events->sctp_address_event) {
				sctp_stcb_feature_on(inp, stcb, SCTP_PCB_FLAGS_RECVPADDREVNT);
			} else {
				sctp_stcb_feature_off(inp, stcb, SCTP_PCB_FLAGS_RECVPADDREVNT);
			}
			if (events->sctp_send_failure_event) {
				sctp_stcb_feature_on(inp, stcb, SCTP_PCB_FLAGS_RECVSENDFAILEVNT);
			} else {
				sctp_stcb_feature_off(inp, stcb, SCTP_PCB_FLAGS_RECVSENDFAILEVNT);
			}
			if (events->sctp_peer_error_event) {
				sctp_stcb_feature_on(inp, stcb, SCTP_PCB_FLAGS_RECVPEERERR);
			} else {
				sctp_stcb_feature_off(inp, stcb, SCTP_PCB_FLAGS_RECVPEERERR);
			}
			if (events->sctp_shutdown_event) {
				sctp_stcb_feature_on(inp, stcb, SCTP_PCB_FLAGS_RECVSHUTDOWNEVNT);
			} else {
				sctp_stcb_feature_off(inp, stcb, SCTP_PCB_FLAGS_RECVSHUTDOWNEVNT);
			}
			if (events->sctp_partial_delivery_event) {
				sctp_stcb_feature_on(inp, stcb, SCTP_PCB_FLAGS_PDAPIEVNT);
			} else {
				sctp_stcb_feature_off(inp, stcb, SCTP_PCB_FLAGS_PDAPIEVNT);
			}
			if (events->sctp_adaptation_layer_event) {
				sctp_stcb_feature_on(inp, stcb, SCTP_PCB_FLAGS_ADAPTATIONEVNT);
			} else {
				sctp_stcb_feature_off(inp, stcb, SCTP_PCB_FLAGS_ADAPTATIONEVNT);
			}
			if (events->sctp_authentication_event) {
				sctp_stcb_feature_on(inp, stcb, SCTP_PCB_FLAGS_AUTHEVNT);
			} else {
				sctp_stcb_feature_off(inp, stcb, SCTP_PCB_FLAGS_AUTHEVNT);
			}
			if (events->sctp_sender_dry_event) {
				sctp_stcb_feature_on(inp, stcb, SCTP_PCB_FLAGS_DRYEVNT);
			} else {
				sctp_stcb_feature_off(inp, stcb, SCTP_PCB_FLAGS_DRYEVNT);
			}
			if (events->sctp_stream_reset_event) {
				sctp_stcb_feature_on(inp, stcb, SCTP_PCB_FLAGS_STREAM_RESETEVNT);
			} else {
				sctp_stcb_feature_off(inp, stcb, SCTP_PCB_FLAGS_STREAM_RESETEVNT);
			}
			SCTP_TCB_UNLOCK(stcb);
		}
		/* Send up the sender dry event only for 1-to-1 style sockets. */
		if (events->sctp_sender_dry_event) {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) {
				stcb = LIST_FIRST(&inp->sctp_asoc_list);
				if (stcb) {
					SCTP_TCB_LOCK(stcb);
					if (TAILQ_EMPTY(&stcb->asoc.send_queue) &&
					    TAILQ_EMPTY(&stcb->asoc.sent_queue) &&
					    (stcb->asoc.stream_queue_cnt == 0)) {
						sctp_ulp_notify(SCTP_NOTIFY_SENDER_DRY, stcb,  0, NULL, SCTP_SO_LOCKED);
					}
					SCTP_TCB_UNLOCK(stcb);
				}
			}
		}
		SCTP_INP_RUNLOCK(inp);
		break;
	}
	case SCTP_ADAPTATION_LAYER:
	{
		struct sctp_setadaptation *adap_bits;

		SCTP_CHECK_AND_CAST(adap_bits, optval, struct sctp_setadaptation, optsize);
		SCTP_INP_WLOCK(inp);
		inp->sctp_ep.adaptation_layer_indicator = adap_bits->ssb_adaptation_ind;
		inp->sctp_ep.adaptation_layer_indicator_provided = 1;
		SCTP_INP_WUNLOCK(inp);
		break;
	}
#ifdef SCTP_DEBUG
	case SCTP_SET_INITIAL_DBG_SEQ:
	{
		uint32_t *vvv;

		SCTP_CHECK_AND_CAST(vvv, optval, uint32_t, optsize);
		SCTP_INP_WLOCK(inp);
		inp->sctp_ep.initial_sequence_debug = *vvv;
		SCTP_INP_WUNLOCK(inp);
		break;
	}
#endif
	case SCTP_DEFAULT_SEND_PARAM:
	{
		struct sctp_sndrcvinfo *s_info;

		SCTP_CHECK_AND_CAST(s_info, optval, struct sctp_sndrcvinfo, optsize);
		SCTP_FIND_STCB(inp, stcb, s_info->sinfo_assoc_id);

		if (stcb) {
			if (s_info->sinfo_stream < stcb->asoc.streamoutcnt) {
				memcpy(&stcb->asoc.def_send, s_info, min(optsize, sizeof(stcb->asoc.def_send)));
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (s_info->sinfo_assoc_id == SCTP_FUTURE_ASSOC) ||
			    (s_info->sinfo_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				memcpy(&inp->def_send, s_info, min(optsize, sizeof(inp->def_send)));
				SCTP_INP_WUNLOCK(inp);
			}
			if ((s_info->sinfo_assoc_id == SCTP_CURRENT_ASSOC) ||
			    (s_info->sinfo_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					if (s_info->sinfo_stream < stcb->asoc.streamoutcnt) {
						memcpy(&stcb->asoc.def_send, s_info, min(optsize, sizeof(stcb->asoc.def_send)));
					}
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}
		}
		break;
	}
	case SCTP_PEER_ADDR_PARAMS:
	{
		struct sctp_paddrparams *paddrp;
		struct sctp_nets *net;

		SCTP_CHECK_AND_CAST(paddrp, optval, struct sctp_paddrparams, optsize);
		SCTP_FIND_STCB(inp, stcb, paddrp->spp_assoc_id);
		net = NULL;
		if (stcb) {
			net = sctp_findnet(stcb, (struct sockaddr *)&paddrp->spp_address);
		} else {
			/* We increment here since sctp_findassociation_ep_addr() wil
			 * do a decrement if it finds the stcb as long as the locked
			 * tcb (last argument) is NOT a TCB.. aka NULL.
			 */
			SCTP_INP_INCR_REF(inp);
			stcb = sctp_findassociation_ep_addr(&inp,
							    (struct sockaddr *)&paddrp->spp_address,
							    &net, NULL, NULL);
			if (stcb == NULL) {
				SCTP_INP_DECR_REF(inp);
			}
		}
		if (stcb && (net == NULL)) {
			struct sockaddr *sa;

			sa = (struct sockaddr *)&paddrp->spp_address;
#ifdef INET
			if (sa->sa_family == AF_INET) {

				struct sockaddr_in *sin;
				sin = (struct sockaddr_in *)sa;
				if (sin->sin_addr.s_addr) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					SCTP_TCB_UNLOCK(stcb);
					error = EINVAL;
					break;
				}
			} else
#endif
#ifdef INET6
			if (sa->sa_family == AF_INET6) {
				struct sockaddr_in6 *sin6;

				sin6 = (struct sockaddr_in6 *)sa;
				if (!IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					SCTP_TCB_UNLOCK(stcb);
					error = EINVAL;
					break;
				}
			} else
#endif
#if defined(__Userspace__)
			if (sa->sa_family == AF_CONN) {
				struct sockaddr_conn *sconn;

				sconn = (struct sockaddr_conn *)sa;
				if (sconn->sconn_addr != NULL) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					SCTP_TCB_UNLOCK(stcb);
					error = EINVAL;
					break;
				}
			} else
#endif
			{
				error = EAFNOSUPPORT;
				SCTP_TCB_UNLOCK(stcb);
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
				break;
			}
		}
		/* sanity checks */
		if ((paddrp->spp_flags & SPP_HB_ENABLE) && (paddrp->spp_flags & SPP_HB_DISABLE)) {
			if (stcb)
				SCTP_TCB_UNLOCK(stcb);
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			return (EINVAL);
		}

		if ((paddrp->spp_flags & SPP_PMTUD_ENABLE) && (paddrp->spp_flags & SPP_PMTUD_DISABLE)) {
			if (stcb)
				SCTP_TCB_UNLOCK(stcb);
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			return (EINVAL);
		}

		if (stcb) {
			/************************TCB SPECIFIC SET ******************/
			/*
			 * do we change the timer for HB, we run
			 * only one?
			 */
			int ovh = 0;

			if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
				ovh = SCTP_MED_OVERHEAD;
			} else {
				ovh = SCTP_MED_V4_OVERHEAD;
			}

			/* network sets ? */
			if (net) {
				/************************NET SPECIFIC SET ******************/
				if (paddrp->spp_flags & SPP_HB_DISABLE) {
					if (!(net->dest_state & SCTP_ADDR_UNCONFIRMED) &&
					    !(net->dest_state & SCTP_ADDR_NOHB)) {
						sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, inp, stcb, net,
								SCTP_FROM_SCTP_USRREQ+SCTP_LOC_10);
					}
					net->dest_state |= SCTP_ADDR_NOHB;
				}
				if (paddrp->spp_flags & SPP_HB_ENABLE) {
					if (paddrp->spp_hbinterval) {
						net->heart_beat_delay = paddrp->spp_hbinterval;
					} else if (paddrp->spp_flags & SPP_HB_TIME_IS_ZERO) {
						net->heart_beat_delay = 0;
					}
					sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, inp, stcb, net,
					                SCTP_FROM_SCTP_USRREQ+SCTP_LOC_10);
					sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, inp, stcb, net);
					net->dest_state &= ~SCTP_ADDR_NOHB;
				}
				if (paddrp->spp_flags & SPP_HB_DEMAND) {
					/* on demand HB */
					sctp_send_hb(stcb, net, SCTP_SO_LOCKED);
					sctp_chunk_output(inp, stcb,  SCTP_OUTPUT_FROM_SOCKOPT, SCTP_SO_LOCKED);
					sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, inp, stcb, net);
				}
				if ((paddrp->spp_flags & SPP_PMTUD_DISABLE) && (paddrp->spp_pathmtu >= SCTP_SMALLEST_PMTU)) {
					if (SCTP_OS_TIMER_PENDING(&net->pmtu_timer.timer)) {
						sctp_timer_stop(SCTP_TIMER_TYPE_PATHMTURAISE, inp, stcb, net,
								SCTP_FROM_SCTP_USRREQ+SCTP_LOC_10);
					}
					net->dest_state |= SCTP_ADDR_NO_PMTUD;
					net->mtu = paddrp->spp_pathmtu + ovh;
					if (net->mtu < stcb->asoc.smallest_mtu) {
						sctp_pathmtu_adjustment(stcb, net->mtu);
					}
				}
				if (paddrp->spp_flags & SPP_PMTUD_ENABLE) {
					if (!SCTP_OS_TIMER_PENDING(&net->pmtu_timer.timer)) {
						sctp_timer_start(SCTP_TIMER_TYPE_PATHMTURAISE, inp, stcb, net);
					}
					net->dest_state &= ~SCTP_ADDR_NO_PMTUD;
				}
				if (paddrp->spp_pathmaxrxt) {
					if (net->dest_state & SCTP_ADDR_PF) {
						if (net->error_count > paddrp->spp_pathmaxrxt) {
							net->dest_state &= ~SCTP_ADDR_PF;
						}
					} else {
						if ((net->error_count <= paddrp->spp_pathmaxrxt) &&
						    (net->error_count > net->pf_threshold)) {
							net->dest_state |= SCTP_ADDR_PF;
							sctp_send_hb(stcb, net, SCTP_SO_LOCKED);
							sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, net, SCTP_FROM_SCTP_TIMER + SCTP_LOC_3);
							sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, net);
						}
					}
					if (net->dest_state & SCTP_ADDR_REACHABLE) {
						if (net->error_count > paddrp->spp_pathmaxrxt) {
					    		net->dest_state &= ~SCTP_ADDR_REACHABLE;
							sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_DOWN, stcb, 0, net, SCTP_SO_LOCKED);
						}
					} else {
						if (net->error_count <= paddrp->spp_pathmaxrxt) {
							net->dest_state |= SCTP_ADDR_REACHABLE;
							sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_UP, stcb, 0, net, SCTP_SO_LOCKED);
						}
					}
					net->failure_threshold = paddrp->spp_pathmaxrxt;
				}
				if (paddrp->spp_flags & SPP_DSCP) {
					net->dscp = paddrp->spp_dscp & 0xfc;
					net->dscp |= 0x01;
				}
#ifdef INET6
				if (paddrp->spp_flags & SPP_IPV6_FLOWLABEL) {
					if (net->ro._l_addr.sa.sa_family == AF_INET6) {
						net->flowlabel = paddrp->spp_ipv6_flowlabel & 0x000fffff;
						net->flowlabel |= 0x80000000;
					}
				}
#endif
			} else {
				/************************ASSOC ONLY -- NO NET SPECIFIC SET ******************/
				if (paddrp->spp_pathmaxrxt) {
					stcb->asoc.def_net_failure = paddrp->spp_pathmaxrxt;
					TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
						if (net->dest_state & SCTP_ADDR_PF) {
							if (net->error_count > paddrp->spp_pathmaxrxt) {
								net->dest_state &= ~SCTP_ADDR_PF;
							}
						} else {
							if ((net->error_count <= paddrp->spp_pathmaxrxt) &&
							    (net->error_count > net->pf_threshold)) {
								net->dest_state |= SCTP_ADDR_PF;
								sctp_send_hb(stcb, net, SCTP_SO_LOCKED);
								sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, net, SCTP_FROM_SCTP_TIMER + SCTP_LOC_3);
								sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, net);
							}
						}
						if (net->dest_state & SCTP_ADDR_REACHABLE) {
							if (net->error_count > paddrp->spp_pathmaxrxt) {
						    		net->dest_state &= ~SCTP_ADDR_REACHABLE;
								sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_DOWN, stcb, 0, net, SCTP_SO_LOCKED);
							}
						} else {
							if (net->error_count <= paddrp->spp_pathmaxrxt) {
								net->dest_state |= SCTP_ADDR_REACHABLE;
								sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_UP, stcb, 0, net, SCTP_SO_LOCKED);
							}
						}
						net->failure_threshold = paddrp->spp_pathmaxrxt;
					}
				}

				if (paddrp->spp_flags & SPP_HB_ENABLE) {
					if (paddrp->spp_hbinterval) {
						stcb->asoc.heart_beat_delay = paddrp->spp_hbinterval;
					} else if (paddrp->spp_flags & SPP_HB_TIME_IS_ZERO) {
						stcb->asoc.heart_beat_delay = 0;
					}
					/* Turn back on the timer */
					TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
						if (paddrp->spp_hbinterval) {
							net->heart_beat_delay = paddrp->spp_hbinterval;
						} else if (paddrp->spp_flags & SPP_HB_TIME_IS_ZERO) {
							net->heart_beat_delay = 0;
						}
						if (net->dest_state & SCTP_ADDR_NOHB) {
							net->dest_state &= ~SCTP_ADDR_NOHB;
						}
						sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, inp, stcb, net,
								SCTP_FROM_SCTP_USRREQ+SCTP_LOC_10);
						sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, inp, stcb, net);
					}
					sctp_stcb_feature_off(inp, stcb, SCTP_PCB_FLAGS_DONOT_HEARTBEAT);
				}
				if (paddrp->spp_flags & SPP_HB_DISABLE) {
					TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
						if (!(net->dest_state & SCTP_ADDR_NOHB)) {
							net->dest_state |= SCTP_ADDR_NOHB;
							if (!(net->dest_state & SCTP_ADDR_UNCONFIRMED)) {
								sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, inp, stcb, net, SCTP_FROM_SCTP_USRREQ+SCTP_LOC_10);
							}
						}
					}
					sctp_stcb_feature_on(inp, stcb, SCTP_PCB_FLAGS_DONOT_HEARTBEAT);
				}
				if ((paddrp->spp_flags & SPP_PMTUD_DISABLE) && (paddrp->spp_pathmtu >= SCTP_SMALLEST_PMTU)) {
					TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
						if (SCTP_OS_TIMER_PENDING(&net->pmtu_timer.timer)) {
							sctp_timer_stop(SCTP_TIMER_TYPE_PATHMTURAISE, inp, stcb, net,
									SCTP_FROM_SCTP_USRREQ+SCTP_LOC_10);
						}
						net->dest_state |= SCTP_ADDR_NO_PMTUD;
						net->mtu = paddrp->spp_pathmtu + ovh;
						if (net->mtu < stcb->asoc.smallest_mtu) {
							sctp_pathmtu_adjustment(stcb, net->mtu);
						}
					}
					sctp_stcb_feature_on(inp, stcb, SCTP_PCB_FLAGS_DO_NOT_PMTUD);
				}
				if (paddrp->spp_flags & SPP_PMTUD_ENABLE) {
					TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
						if (!SCTP_OS_TIMER_PENDING(&net->pmtu_timer.timer)) {
							sctp_timer_start(SCTP_TIMER_TYPE_PATHMTURAISE, inp, stcb, net);
						}
						net->dest_state &= ~SCTP_ADDR_NO_PMTUD;
					}
					sctp_stcb_feature_off(inp, stcb, SCTP_PCB_FLAGS_DO_NOT_PMTUD);
				}
				if (paddrp->spp_flags & SPP_DSCP) {
					TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
						net->dscp = paddrp->spp_dscp & 0xfc;
						net->dscp |= 0x01;
					}
					stcb->asoc.default_dscp = paddrp->spp_dscp & 0xfc;
					stcb->asoc.default_dscp |= 0x01;
				}
#ifdef INET6
				if (paddrp->spp_flags & SPP_IPV6_FLOWLABEL) {
					TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
						if (net->ro._l_addr.sa.sa_family == AF_INET6) {
							net->flowlabel = paddrp->spp_ipv6_flowlabel & 0x000fffff;
							net->flowlabel |= 0x80000000;
						}
					}
					stcb->asoc.default_flowlabel = paddrp->spp_ipv6_flowlabel & 0x000fffff;
					stcb->asoc.default_flowlabel |= 0x80000000;
				}
#endif
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			/************************NO TCB, SET TO default stuff ******************/
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (paddrp->spp_assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				/*
				 * For the TOS/FLOWLABEL stuff you set it
				 * with the options on the socket
				 */
				if (paddrp->spp_pathmaxrxt) {
					inp->sctp_ep.def_net_failure = paddrp->spp_pathmaxrxt;
				}

				if (paddrp->spp_flags & SPP_HB_TIME_IS_ZERO)
					inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_HEARTBEAT] = 0;
				else if (paddrp->spp_hbinterval) {
					if (paddrp->spp_hbinterval > SCTP_MAX_HB_INTERVAL)
						paddrp->spp_hbinterval= SCTP_MAX_HB_INTERVAL;
					inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_HEARTBEAT] = MSEC_TO_TICKS(paddrp->spp_hbinterval);
				}

				if (paddrp->spp_flags & SPP_HB_ENABLE) {
					if (paddrp->spp_flags & SPP_HB_TIME_IS_ZERO) {
						inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_HEARTBEAT] = 0;
					} else if (paddrp->spp_hbinterval) {
						inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_HEARTBEAT] = MSEC_TO_TICKS(paddrp->spp_hbinterval);
					}
					sctp_feature_off(inp, SCTP_PCB_FLAGS_DONOT_HEARTBEAT);
				} else if (paddrp->spp_flags & SPP_HB_DISABLE) {
					sctp_feature_on(inp, SCTP_PCB_FLAGS_DONOT_HEARTBEAT);
				}
				if (paddrp->spp_flags & SPP_PMTUD_ENABLE) {
					sctp_feature_off(inp, SCTP_PCB_FLAGS_DO_NOT_PMTUD);
				} else if (paddrp->spp_flags & SPP_PMTUD_DISABLE) {
					sctp_feature_on(inp, SCTP_PCB_FLAGS_DO_NOT_PMTUD);
				}
				if (paddrp->spp_flags & SPP_DSCP) {
					inp->sctp_ep.default_dscp = paddrp->spp_dscp & 0xfc;
					inp->sctp_ep.default_dscp |= 0x01;
				}
#ifdef INET6
				if (paddrp->spp_flags & SPP_IPV6_FLOWLABEL) {
					if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
						inp->sctp_ep.default_flowlabel = paddrp->spp_ipv6_flowlabel & 0x000fffff;
						inp->sctp_ep.default_flowlabel |= 0x80000000;
					}
				}
#endif
				SCTP_INP_WUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		break;
	}
	case SCTP_RTOINFO:
	{
		struct sctp_rtoinfo *srto;
		uint32_t new_init, new_min, new_max;

		SCTP_CHECK_AND_CAST(srto, optval, struct sctp_rtoinfo, optsize);
		SCTP_FIND_STCB(inp, stcb, srto->srto_assoc_id);

		if (stcb) {
			if (srto->srto_initial)
				new_init = srto->srto_initial;
			else
				new_init = stcb->asoc.initial_rto;
			if (srto->srto_max)
				new_max = srto->srto_max;
			else
				new_max = stcb->asoc.maxrto;
			if (srto->srto_min)
				new_min = srto->srto_min;
			else
				new_min = stcb->asoc.minrto;
			if ((new_min <= new_init) && (new_init <= new_max)) {
				stcb->asoc.initial_rto = new_init;
				stcb->asoc.maxrto = new_max;
				stcb->asoc.minrto = new_min;
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (srto->srto_assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				if (srto->srto_initial)
					new_init = srto->srto_initial;
				else
					new_init = inp->sctp_ep.initial_rto;
				if (srto->srto_max)
					new_max = srto->srto_max;
				else
					new_max = inp->sctp_ep.sctp_maxrto;
				if (srto->srto_min)
					new_min = srto->srto_min;
				else
					new_min = inp->sctp_ep.sctp_minrto;
				if ((new_min <= new_init) && (new_init <= new_max)) {
					inp->sctp_ep.initial_rto = new_init;
					inp->sctp_ep.sctp_maxrto = new_max;
					inp->sctp_ep.sctp_minrto = new_min;
				} else {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					error = EINVAL;
				}
				SCTP_INP_WUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		break;
	}
	case SCTP_ASSOCINFO:
	{
		struct sctp_assocparams *sasoc;

		SCTP_CHECK_AND_CAST(sasoc, optval, struct sctp_assocparams, optsize);
		SCTP_FIND_STCB(inp, stcb, sasoc->sasoc_assoc_id);
		if (sasoc->sasoc_cookie_life) {
			/* boundary check the cookie life */
			if (sasoc->sasoc_cookie_life < 1000)
				sasoc->sasoc_cookie_life = 1000;
			if (sasoc->sasoc_cookie_life > SCTP_MAX_COOKIE_LIFE) {
				sasoc->sasoc_cookie_life = SCTP_MAX_COOKIE_LIFE;
			}
		}
		if (stcb) {
			if (sasoc->sasoc_asocmaxrxt)
				stcb->asoc.max_send_times = sasoc->sasoc_asocmaxrxt;
			if (sasoc->sasoc_cookie_life) {
				stcb->asoc.cookie_life = MSEC_TO_TICKS(sasoc->sasoc_cookie_life);
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (sasoc->sasoc_assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				if (sasoc->sasoc_asocmaxrxt)
					inp->sctp_ep.max_send_times = sasoc->sasoc_asocmaxrxt;
				if (sasoc->sasoc_cookie_life) {
					inp->sctp_ep.def_cookie_life = MSEC_TO_TICKS(sasoc->sasoc_cookie_life);
				}
				SCTP_INP_WUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		break;
	}
	case SCTP_INITMSG:
	{
		struct sctp_initmsg *sinit;

		SCTP_CHECK_AND_CAST(sinit, optval, struct sctp_initmsg, optsize);
		SCTP_INP_WLOCK(inp);
		if (sinit->sinit_num_ostreams)
			inp->sctp_ep.pre_open_stream_count = sinit->sinit_num_ostreams;

		if (sinit->sinit_max_instreams)
			inp->sctp_ep.max_open_streams_intome = sinit->sinit_max_instreams;

		if (sinit->sinit_max_attempts)
			inp->sctp_ep.max_init_times = sinit->sinit_max_attempts;

		if (sinit->sinit_max_init_timeo)
			inp->sctp_ep.initial_init_rto_max = sinit->sinit_max_init_timeo;
		SCTP_INP_WUNLOCK(inp);
		break;
	}
	case SCTP_PRIMARY_ADDR:
	{
		struct sctp_setprim *spa;
		struct sctp_nets *net;

		SCTP_CHECK_AND_CAST(spa, optval, struct sctp_setprim, optsize);
		SCTP_FIND_STCB(inp, stcb, spa->ssp_assoc_id);

		net = NULL;
		if (stcb) {
			net = sctp_findnet(stcb, (struct sockaddr *)&spa->ssp_addr);
		} else {
			/* We increment here since sctp_findassociation_ep_addr() wil
			 * do a decrement if it finds the stcb as long as the locked
			 * tcb (last argument) is NOT a TCB.. aka NULL.
			 */
			SCTP_INP_INCR_REF(inp);
			stcb = sctp_findassociation_ep_addr(&inp,
							    (struct sockaddr *)&spa->ssp_addr,
							    &net, NULL, NULL);
			if (stcb == NULL) {
				SCTP_INP_DECR_REF(inp);
			}
		}

		if ((stcb) && (net)) {
			if ((net != stcb->asoc.primary_destination) &&
			    (!(net->dest_state & SCTP_ADDR_UNCONFIRMED))) {
				/* Ok we need to set it */
				if (sctp_set_primary_addr(stcb, (struct sockaddr *)NULL, net) == 0) {
					if ((stcb->asoc.alternate) &&
					    (!(net->dest_state & SCTP_ADDR_PF)) &&
					    (net->dest_state & SCTP_ADDR_REACHABLE)) {
						sctp_free_remote_addr(stcb->asoc.alternate);
						stcb->asoc.alternate = NULL;
					}
				}
			}
		} else {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
		}
		if (stcb) {
			SCTP_TCB_UNLOCK(stcb);
		}
		break;
	}
	case SCTP_SET_DYNAMIC_PRIMARY:
	{
		union sctp_sockstore *ss;
#ifdef SCTP_MVRF
		int i, fnd = 0;
#endif
#if !defined(__Windows__) && !defined(__Userspace__)
#if defined(__APPLE__)
		struct proc *proc;
#endif
#ifdef __FreeBSD__
#if __FreeBSD_version > 602000
		error = priv_check(curthread,
				   PRIV_NETINET_RESERVEDPORT);
#elif __FreeBSD_version >= 500000
		error = suser((struct thread *)p);
#else
		error = suser(p);
#endif
#elif defined(__APPLE__)
		proc = (struct proc *)p;
		if (p) {
			error = suser(proc->p_ucred, &proc->p_acflag);
		} else {
			break;
		}
#else
		error = suser(p, 0);
#endif
#endif
		if (error)
			break;

		SCTP_CHECK_AND_CAST(ss, optval, union sctp_sockstore, optsize);
		/* SUPER USER CHECK? */
#ifdef SCTP_MVRF
		for (i = 0; i < inp->num_vrfs; i++) {
			if (vrf_id == inp->m_vrf_ids[i]) {
				fnd = 1;
				break;
			}
		}
		if (!fnd) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
#endif
		error = sctp_dynamic_set_primary(&ss->sa, vrf_id);
		break;
	}
	case SCTP_SET_PEER_PRIMARY_ADDR:
	{
		struct sctp_setpeerprim *sspp;

		SCTP_CHECK_AND_CAST(sspp, optval, struct sctp_setpeerprim, optsize);
		SCTP_FIND_STCB(inp, stcb, sspp->sspp_assoc_id);
		if (stcb != NULL) {
			struct sctp_ifa *ifa;
			ifa = sctp_find_ifa_by_addr((struct sockaddr *)&sspp->sspp_addr,
						    stcb->asoc.vrf_id, SCTP_ADDR_NOT_LOCKED);
			if (ifa == NULL) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
				goto out_of_it;
			}
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) == 0) {
				/* Must validate the ifa found is in our ep */
				struct sctp_laddr *laddr;
				int found = 0;
				LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
					if (laddr->ifa == NULL) {
						SCTPDBG(SCTP_DEBUG_OUTPUT1, "%s: NULL ifa\n",
							__FUNCTION__);
						continue;
					}
					if (laddr->ifa == ifa) {
						found = 1;
						break;
					}
				}
				if (!found) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					error = EINVAL;
					goto out_of_it;
				}
			}
			if (sctp_set_primary_ip_address_sa(stcb,
							   (struct sockaddr *)&sspp->sspp_addr) != 0) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		out_of_it:
			SCTP_TCB_UNLOCK(stcb);
		} else {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
		}
		break;
	}
	case SCTP_BINDX_ADD_ADDR:
	{
		struct sctp_getaddresses *addrs;
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
		struct thread *td;

		td = (struct thread *)p;
#endif
		SCTP_CHECK_AND_CAST(addrs, optval, struct sctp_getaddresses,
				    optsize);
#ifdef INET
		if (addrs->addr->sa_family == AF_INET) {
			if (optsize < sizeof(struct sctp_getaddresses) - sizeof(struct sockaddr) + sizeof(struct sockaddr_in)) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
				break;
			}
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
			if (td != NULL && (error = prison_local_ip4(td->td_ucred, &(((struct sockaddr_in *)(addrs->addr))->sin_addr)))) {
				SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_USRREQ, error);
				break;
			}
#endif
		} else
#endif
#ifdef INET6
		if (addrs->addr->sa_family == AF_INET6) {
			if (optsize < sizeof(struct sctp_getaddresses) - sizeof(struct sockaddr) + sizeof(struct sockaddr_in6)) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
				break;
			}
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
			if (td != NULL && (error = prison_local_ip6(td->td_ucred, &(((struct sockaddr_in6 *)(addrs->addr))->sin6_addr),
											   (SCTP_IPV6_V6ONLY(inp) != 0))) != 0) {
			  SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_USRREQ, error);
			  break;
			}
#endif
		} else
#endif
		{
		       error = EAFNOSUPPORT;
  		       break;
		}
		sctp_bindx_add_address(so, inp, addrs->addr,
				       addrs->sget_assoc_id, vrf_id,
				       &error, p);
		break;
	}
	case SCTP_BINDX_REM_ADDR:
	{
		struct sctp_getaddresses *addrs;
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
		struct thread *td;
		td = (struct thread *)p;

#endif
		SCTP_CHECK_AND_CAST(addrs, optval, struct sctp_getaddresses, optsize);
#ifdef INET
		if (addrs->addr->sa_family == AF_INET) {
			if (optsize < sizeof(struct sctp_getaddresses) - sizeof(struct sockaddr) + sizeof(struct sockaddr_in)) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
				break;
			}
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
		if (td != NULL && (error = prison_local_ip4(td->td_ucred, &(((struct sockaddr_in *)(addrs->addr))->sin_addr)))) {
				SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_USRREQ, error);
				break;
			}
#endif
		} else
#endif
#ifdef INET6
		if (addrs->addr->sa_family == AF_INET6) {
			if (optsize < sizeof(struct sctp_getaddresses) - sizeof(struct sockaddr) + sizeof(struct sockaddr_in6)) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
				break;
			}
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
			if (td != NULL &&
			    (error = prison_local_ip6(td->td_ucred,
			                              &(((struct sockaddr_in6 *)(addrs->addr))->sin6_addr),
			                              (SCTP_IPV6_V6ONLY(inp) != 0))) != 0) {
				SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_USRREQ, error);
				break;
			}
#endif
		} else
#endif
		{
			error = EAFNOSUPPORT;
			break;
		}
		sctp_bindx_delete_address(inp, addrs->addr,
					  addrs->sget_assoc_id, vrf_id,
					  &error);
		break;
	}
#ifdef __APPLE__
	case SCTP_LISTEN_FIX:
		/* only applies to one-to-many sockets */
		if (inp->sctp_flags & SCTP_PCB_FLAGS_UDPTYPE) {
			/* make sure the ACCEPTCONN flag is OFF */
			so->so_options &= ~SO_ACCEPTCONN;
		} else {
			/* otherwise, not allowed */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
		}
		break;
#endif				/* __APPLE__ */
	case SCTP_EVENT:
	{
		struct sctp_event *event;
		uint32_t event_type;

		SCTP_CHECK_AND_CAST(event, optval, struct sctp_event, optsize);
		SCTP_FIND_STCB(inp, stcb, event->se_assoc_id);
		switch (event->se_type) {
		case SCTP_ASSOC_CHANGE:
			event_type = SCTP_PCB_FLAGS_RECVASSOCEVNT;
			break;
		case SCTP_PEER_ADDR_CHANGE:
			event_type = SCTP_PCB_FLAGS_RECVPADDREVNT;
			break;
		case SCTP_REMOTE_ERROR:
			event_type = SCTP_PCB_FLAGS_RECVPEERERR;
			break;
		case SCTP_SEND_FAILED:
			event_type = SCTP_PCB_FLAGS_RECVSENDFAILEVNT;
			break;
		case SCTP_SHUTDOWN_EVENT:
			event_type = SCTP_PCB_FLAGS_RECVSHUTDOWNEVNT;
			break;
		case SCTP_ADAPTATION_INDICATION:
			event_type = SCTP_PCB_FLAGS_ADAPTATIONEVNT;
			break;
		case SCTP_PARTIAL_DELIVERY_EVENT:
			event_type = SCTP_PCB_FLAGS_PDAPIEVNT;
			break;
		case SCTP_AUTHENTICATION_EVENT:
			event_type = SCTP_PCB_FLAGS_AUTHEVNT;
			break;
		case SCTP_STREAM_RESET_EVENT:
			event_type = SCTP_PCB_FLAGS_STREAM_RESETEVNT;
			break;
		case SCTP_SENDER_DRY_EVENT:
			event_type = SCTP_PCB_FLAGS_DRYEVNT;
			break;
		case SCTP_NOTIFICATIONS_STOPPED_EVENT:
			event_type = 0;
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOTSUP);
			error = ENOTSUP;
			break;
		case SCTP_ASSOC_RESET_EVENT:
			event_type = SCTP_PCB_FLAGS_ASSOC_RESETEVNT;
			break;
		case SCTP_STREAM_CHANGE_EVENT:
			event_type = SCTP_PCB_FLAGS_STREAM_CHANGEEVNT;
			break;
		case SCTP_SEND_FAILED_EVENT:
			event_type = SCTP_PCB_FLAGS_RECVNSENDFAILEVNT;
			break;
		default:
			event_type = 0;
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		if (event_type > 0) {
			if (stcb) {
				if (event->se_on) {
					sctp_stcb_feature_on(inp, stcb, event_type);
					if (event_type == SCTP_PCB_FLAGS_DRYEVNT) {
						if (TAILQ_EMPTY(&stcb->asoc.send_queue) &&
						    TAILQ_EMPTY(&stcb->asoc.sent_queue) &&
						    (stcb->asoc.stream_queue_cnt == 0)) {
							sctp_ulp_notify(SCTP_NOTIFY_SENDER_DRY, stcb,  0, NULL, SCTP_SO_LOCKED);
						}
					}
				} else {
					sctp_stcb_feature_off(inp, stcb, event_type);
				}
				SCTP_TCB_UNLOCK(stcb);
			} else {
				/*
				 * We don't want to send up a storm of events,
				 * so return an error for sender dry events
				 */
				if ((event_type == SCTP_PCB_FLAGS_DRYEVNT) &&
				    ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) == 0) &&
				    ((inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) == 0) &&
				    ((event->se_assoc_id == SCTP_ALL_ASSOC) ||
				     (event->se_assoc_id == SCTP_CURRENT_ASSOC))) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOTSUP);
					error = ENOTSUP;
					break;
				}
				if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
				    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
				    (event->se_assoc_id == SCTP_FUTURE_ASSOC) ||
				    (event->se_assoc_id == SCTP_ALL_ASSOC)) {
					SCTP_INP_WLOCK(inp);
					if (event->se_on) {
						sctp_feature_on(inp, event_type);
					} else {
						sctp_feature_off(inp, event_type);
					}
					SCTP_INP_WUNLOCK(inp);
				}
				if ((event->se_assoc_id == SCTP_CURRENT_ASSOC) ||
				    (event->se_assoc_id == SCTP_ALL_ASSOC)) {
					SCTP_INP_RLOCK(inp);
					LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
						SCTP_TCB_LOCK(stcb);
						if (event->se_on) {
							sctp_stcb_feature_on(inp, stcb, event_type);
						} else {
							sctp_stcb_feature_off(inp, stcb, event_type);
						}
						SCTP_TCB_UNLOCK(stcb);
					}
					SCTP_INP_RUNLOCK(inp);
				}
			}
		}
		break;
	}
	case SCTP_RECVRCVINFO:
	{
		int *onoff;

		SCTP_CHECK_AND_CAST(onoff, optval, int, optsize);
		SCTP_INP_WLOCK(inp);
		if (*onoff != 0) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_RECVRCVINFO);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_RECVRCVINFO);
		}
		SCTP_INP_WUNLOCK(inp);
		break;
	}
	case SCTP_RECVNXTINFO:
	{
		int *onoff;

		SCTP_CHECK_AND_CAST(onoff, optval, int, optsize);
		SCTP_INP_WLOCK(inp);
		if (*onoff != 0) {
			sctp_feature_on(inp, SCTP_PCB_FLAGS_RECVNXTINFO);
		} else {
			sctp_feature_off(inp, SCTP_PCB_FLAGS_RECVNXTINFO);
		}
		SCTP_INP_WUNLOCK(inp);
		break;
	}
	case SCTP_DEFAULT_SNDINFO:
	{
		struct sctp_sndinfo *info;
		uint16_t policy;

		SCTP_CHECK_AND_CAST(info, optval, struct sctp_sndinfo, optsize);
		SCTP_FIND_STCB(inp, stcb, info->snd_assoc_id);

		if (stcb) {
			if (info->snd_sid < stcb->asoc.streamoutcnt) {
				stcb->asoc.def_send.sinfo_stream = info->snd_sid;
				policy = PR_SCTP_POLICY(stcb->asoc.def_send.sinfo_flags);
				stcb->asoc.def_send.sinfo_flags = info->snd_flags;
				stcb->asoc.def_send.sinfo_flags |= policy;
				stcb->asoc.def_send.sinfo_ppid = info->snd_ppid;
				stcb->asoc.def_send.sinfo_context = info->snd_context;
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (info->snd_assoc_id == SCTP_FUTURE_ASSOC) ||
			    (info->snd_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				inp->def_send.sinfo_stream = info->snd_sid;
				policy = PR_SCTP_POLICY(inp->def_send.sinfo_flags);
				inp->def_send.sinfo_flags = info->snd_flags;
				inp->def_send.sinfo_flags |= policy;
				inp->def_send.sinfo_ppid = info->snd_ppid;
				inp->def_send.sinfo_context = info->snd_context;
				SCTP_INP_WUNLOCK(inp);
			}
			if ((info->snd_assoc_id == SCTP_CURRENT_ASSOC) ||
			    (info->snd_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					if (info->snd_sid < stcb->asoc.streamoutcnt) {
						stcb->asoc.def_send.sinfo_stream = info->snd_sid;
						policy = PR_SCTP_POLICY(stcb->asoc.def_send.sinfo_flags);
						stcb->asoc.def_send.sinfo_flags = info->snd_flags;
						stcb->asoc.def_send.sinfo_flags |= policy;
						stcb->asoc.def_send.sinfo_ppid = info->snd_ppid;
						stcb->asoc.def_send.sinfo_context = info->snd_context;
					}
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}
		}
		break;
	}
	case SCTP_DEFAULT_PRINFO:
	{
		struct sctp_default_prinfo *info;

		SCTP_CHECK_AND_CAST(info, optval, struct sctp_default_prinfo, optsize);
		SCTP_FIND_STCB(inp, stcb, info->pr_assoc_id);

		if (PR_SCTP_INVALID_POLICY(info->pr_policy)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			error = EINVAL;
			break;
		}
		if (stcb) {
			stcb->asoc.def_send.sinfo_flags &= 0xfff0;
			stcb->asoc.def_send.sinfo_flags |= info->pr_policy;
			stcb->asoc.def_send.sinfo_timetolive = info->pr_value;
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (info->pr_assoc_id == SCTP_FUTURE_ASSOC) ||
			    (info->pr_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				inp->def_send.sinfo_flags &= 0xfff0;
				inp->def_send.sinfo_flags |= info->pr_policy;
				inp->def_send.sinfo_timetolive = info->pr_value;
				SCTP_INP_WUNLOCK(inp);
			}
			if ((info->pr_assoc_id == SCTP_CURRENT_ASSOC) ||
			    (info->pr_assoc_id == SCTP_ALL_ASSOC)) {
				SCTP_INP_RLOCK(inp);
				LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
					SCTP_TCB_LOCK(stcb);
					stcb->asoc.def_send.sinfo_flags &= 0xfff0;
					stcb->asoc.def_send.sinfo_flags |= info->pr_policy;
					stcb->asoc.def_send.sinfo_timetolive = info->pr_value;
					SCTP_TCB_UNLOCK(stcb);
				}
				SCTP_INP_RUNLOCK(inp);
			}
		}
		break;
	}
	case SCTP_PEER_ADDR_THLDS:
		/* Applies to the specific association */
	{
		struct sctp_paddrthlds *thlds;
		struct sctp_nets *net;

		SCTP_CHECK_AND_CAST(thlds, optval, struct sctp_paddrthlds, optsize);
		SCTP_FIND_STCB(inp, stcb, thlds->spt_assoc_id);
		net = NULL;
		if (stcb) {
			net = sctp_findnet(stcb, (struct sockaddr *)&thlds->spt_assoc_id);
		} else {
			/* We increment here since sctp_findassociation_ep_addr() wil
			 * do a decrement if it finds the stcb as long as the locked
			 * tcb (last argument) is NOT a TCB.. aka NULL.
			 */
			SCTP_INP_INCR_REF(inp);
			stcb = sctp_findassociation_ep_addr(&inp,
							    (struct sockaddr *)&thlds->spt_assoc_id,
							    &net, NULL, NULL);
			if (stcb == NULL) {
				SCTP_INP_DECR_REF(inp);
			}
		}
		if (stcb && (net == NULL)) {
			struct sockaddr *sa;

			sa = (struct sockaddr *)&thlds->spt_assoc_id;
#ifdef INET
			if (sa->sa_family == AF_INET) {

				struct sockaddr_in *sin;
				sin = (struct sockaddr_in *)sa;
				if (sin->sin_addr.s_addr) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					SCTP_TCB_UNLOCK(stcb);
					error = EINVAL;
					break;
				}
			} else
#endif
#ifdef INET6
			if (sa->sa_family == AF_INET6) {
				struct sockaddr_in6 *sin6;

				sin6 = (struct sockaddr_in6 *)sa;
				if (!IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					SCTP_TCB_UNLOCK(stcb);
					error = EINVAL;
					break;
				}
			} else
#endif
#if defined(__Userspace__)
			if (sa->sa_family == AF_CONN) {
				struct sockaddr_conn *sconn;

				sconn = (struct sockaddr_conn *)sa;
				if (sconn->sconn_addr != NULL) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					SCTP_TCB_UNLOCK(stcb);
					error = EINVAL;
					break;
				}
			} else
#endif
			{
				error = EAFNOSUPPORT;
				SCTP_TCB_UNLOCK(stcb);
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
				break;
			}
		}
		if (stcb) {
			if (net) {
				if (net->dest_state & SCTP_ADDR_PF) {
					if ((net->failure_threshold > thlds->spt_pathmaxrxt) ||
					    (net->failure_threshold <= thlds->spt_pathpfthld)) {
						net->dest_state &= ~SCTP_ADDR_PF;
					}
				} else {
					if ((net->failure_threshold > thlds->spt_pathpfthld) &&
					    (net->failure_threshold <= thlds->spt_pathmaxrxt)) {
					    	net->dest_state |= SCTP_ADDR_PF;
						sctp_send_hb(stcb, net, SCTP_SO_LOCKED);
						sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, net, SCTP_FROM_SCTP_TIMER + SCTP_LOC_3);
						sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, net);
					}
				}
				if (net->dest_state & SCTP_ADDR_REACHABLE) {
					if (net->failure_threshold > thlds->spt_pathmaxrxt) {
						net->dest_state &= ~SCTP_ADDR_REACHABLE;
						sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_DOWN, stcb, 0, net, SCTP_SO_LOCKED);
					}
				} else {
					if (net->failure_threshold <= thlds->spt_pathmaxrxt) {
						net->dest_state |= SCTP_ADDR_REACHABLE;
						sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_UP, stcb, 0, net, SCTP_SO_LOCKED);
					}
				}
				net->failure_threshold = thlds->spt_pathmaxrxt;
				net->pf_threshold = thlds->spt_pathpfthld;
			} else {
				TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
					if (net->dest_state & SCTP_ADDR_PF) {
						if ((net->failure_threshold > thlds->spt_pathmaxrxt) ||
						    (net->failure_threshold <= thlds->spt_pathpfthld)) {
							net->dest_state &= ~SCTP_ADDR_PF;
						}
					} else {
						if ((net->failure_threshold > thlds->spt_pathpfthld) &&
						    (net->failure_threshold <= thlds->spt_pathmaxrxt)) {
						    	net->dest_state |= SCTP_ADDR_PF;
							sctp_send_hb(stcb, net, SCTP_SO_LOCKED);
							sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, net, SCTP_FROM_SCTP_TIMER + SCTP_LOC_3);
							sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, net);
						}
					}
					if (net->dest_state & SCTP_ADDR_REACHABLE) {
						if (net->failure_threshold > thlds->spt_pathmaxrxt) {
							net->dest_state &= ~SCTP_ADDR_REACHABLE;
							sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_DOWN, stcb, 0, net, SCTP_SO_LOCKED);
						}
					} else {
						if (net->failure_threshold <= thlds->spt_pathmaxrxt) {
							net->dest_state |= SCTP_ADDR_REACHABLE;
							sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_UP, stcb, 0, net, SCTP_SO_LOCKED);
						}
					}
					net->failure_threshold = thlds->spt_pathmaxrxt;
					net->pf_threshold = thlds->spt_pathpfthld;
				}
				stcb->asoc.def_net_failure = thlds->spt_pathmaxrxt;
				stcb->asoc.def_net_pf_threshold = thlds->spt_pathpfthld;
			}
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (thlds->spt_assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				inp->sctp_ep.def_net_failure = thlds->spt_pathmaxrxt;
				inp->sctp_ep.def_net_pf_threshold = thlds->spt_pathpfthld;
				SCTP_INP_WUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		break;
	}
	case SCTP_REMOTE_UDP_ENCAPS_PORT:
	{
		struct sctp_udpencaps *encaps;
		struct sctp_nets *net;

		SCTP_CHECK_AND_CAST(encaps, optval, struct sctp_udpencaps, optsize);
		SCTP_FIND_STCB(inp, stcb, encaps->sue_assoc_id);
		if (stcb) {
			net = sctp_findnet(stcb, (struct sockaddr *)&encaps->sue_address);
		} else {
			/* We increment here since sctp_findassociation_ep_addr() wil
			 * do a decrement if it finds the stcb as long as the locked
			 * tcb (last argument) is NOT a TCB.. aka NULL.
			 */
			net = NULL;
			SCTP_INP_INCR_REF(inp);
			stcb = sctp_findassociation_ep_addr(&inp, (struct sockaddr *)&encaps->sue_address, &net, NULL, NULL);
			if (stcb == NULL) {
				SCTP_INP_DECR_REF(inp);
			}
		}
		if (stcb && (net == NULL)) {
			struct sockaddr *sa;

			sa = (struct sockaddr *)&encaps->sue_address;
#ifdef INET
			if (sa->sa_family == AF_INET) {

				struct sockaddr_in *sin;
				sin = (struct sockaddr_in *)sa;
				if (sin->sin_addr.s_addr) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					SCTP_TCB_UNLOCK(stcb);
					error = EINVAL;
					break;
				}
			} else
#endif
#ifdef INET6
			if (sa->sa_family == AF_INET6) {
				struct sockaddr_in6 *sin6;

				sin6 = (struct sockaddr_in6 *)sa;
				if (!IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					SCTP_TCB_UNLOCK(stcb);
					error = EINVAL;
					break;
				}
			} else
#endif
#if defined(__Userspace__)
			if (sa->sa_family == AF_CONN) {
				struct sockaddr_conn *sconn;

				sconn = (struct sockaddr_conn *)sa;
				if (sconn->sconn_addr != NULL) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
					SCTP_TCB_UNLOCK(stcb);
					error = EINVAL;
					break;
				}
			} else
#endif
			{
					error = EAFNOSUPPORT;
					SCTP_TCB_UNLOCK(stcb);
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
					break;
				}
		}

		if (stcb) {
			if (net) {
				net->port = encaps->sue_port;
			} else {
				stcb->asoc.port = encaps->sue_port;
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
			    (encaps->sue_assoc_id == SCTP_FUTURE_ASSOC)) {
				SCTP_INP_WLOCK(inp);
				inp->sctp_ep.port = encaps->sue_port;
				SCTP_INP_WUNLOCK(inp);
			} else {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
				error = EINVAL;
			}
		}
		break;
	}
	default:
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOPROTOOPT);
		error = ENOPROTOOPT;
		break;
	} /* end switch (opt) */
	return (error);
}

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
int
sctp_ctloutput(struct socket *so, struct sockopt *sopt)
{
	void *optval = NULL;
	size_t optsize = 0;
	void *p;
	int error = 0;

	if (sopt->sopt_level != IPPROTO_SCTP) {
		/* wrong proto level... send back up to IP */
#ifdef INET6
		if (INP_CHECK_SOCKAF(so, AF_INET6))
			error = ip6_ctloutput(so, sopt);
#endif				/* INET6 */
#if defined(INET) && defined(INET6)
		else
#endif
#ifdef INET
			error = ip_ctloutput(so, sopt);
#endif
		return (error);
	}
	optsize = sopt->sopt_valsize;
	if (optsize) {
		SCTP_MALLOC(optval, void *, optsize, SCTP_M_SOCKOPT);
		if (optval == NULL) {
			SCTP_LTRACE_ERR_RET(so->so_pcb, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOBUFS);
			return (ENOBUFS);
		}
		error = sooptcopyin(sopt, optval, optsize, optsize);
		if (error) {
			SCTP_FREE(optval, SCTP_M_SOCKOPT);
			goto out;
		}
	}
#if (defined(__FreeBSD__) && __FreeBSD_version >= 500000) || defined(__Windows__)
	p = (void *)sopt->sopt_td;
#else
	p = (void *)sopt->sopt_p;
#endif
	if (sopt->sopt_dir == SOPT_SET) {
		error = sctp_setopt(so, sopt->sopt_name, optval, optsize, p);
	} else if (sopt->sopt_dir == SOPT_GET) {
		error = sctp_getopt(so, sopt->sopt_name, optval, &optsize, p);
	} else {
		SCTP_LTRACE_ERR_RET(so->so_pcb, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		error = EINVAL;
	}
	if ((error == 0) && (optval != NULL)) {
		error = sooptcopyout(sopt, optval, optsize);
		SCTP_FREE(optval, SCTP_M_SOCKOPT);
	} else if (optval != NULL) {
		SCTP_FREE(optval, SCTP_M_SOCKOPT);
	}
out:
	return (error);
}
#endif

#ifdef INET
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
static int
sctp_connect(struct socket *so, struct sockaddr *addr, struct thread *p)
{
#else
#if defined(__FreeBSD__) || defined(__APPLE__)
static int
sctp_connect(struct socket *so, struct sockaddr *addr, struct proc *p)
{
#elif defined(__Panda__) || defined(__Userspace__)
int
sctp_connect(struct socket *so, struct sockaddr *addr)
{
	void *p = NULL;
#elif defined(__Windows__)
static int
sctp_connect(struct socket *so, struct sockaddr *addr, PKTHREAD p)
{
#else
static int
sctp_connect(struct socket *so, struct mbuf *nam, struct proc *p)
{
	struct sockaddr *addr = mtod(nam, struct sockaddr *);

#endif
#endif
#ifdef SCTP_MVRF
	int i, fnd = 0;
#endif
	int error = 0;
	int create_lock_on = 0;
	uint32_t vrf_id;
	struct sctp_inpcb *inp;
	struct sctp_tcb *stcb = NULL;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		/* I made the same as TCP since we are not setup? */
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (ECONNRESET);
	}
	if (addr == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return EINVAL;
	}

#if defined(__Userspace__)
        /* TODO __Userspace__ falls into this code for IPv6 stuff at the moment... */
#endif
#if !defined(__Windows__) && !defined(__Userspace_os_Linux) && !defined(__Userspace_os_Windows)
	switch (addr->sa_family) {
#ifdef INET6
	case AF_INET6:
	{
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
		struct sockaddr_in6 *sin6p;

#endif
		if (addr->sa_len != sizeof(struct sockaddr_in6)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			return (EINVAL);
		}
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
		sin6p = (struct sockaddr_in6 *)addr;
		if (p != NULL && (error = prison_remote_ip6(p->td_ucred, &sin6p->sin6_addr)) != 0) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
			return (error);
		}
#endif
		break;
	}
#endif
#ifdef INET
	case AF_INET:
	{
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
		struct sockaddr_in *sinp;

#endif
#if !defined(__Userspace_os_Windows)
		if (addr->sa_len != sizeof(struct sockaddr_in)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			return (EINVAL);
		}
#endif
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
		sinp = (struct sockaddr_in *)addr;
		if (p != NULL && (error = prison_remote_ip4(p->td_ucred, &sinp->sin_addr)) != 0) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, error);
			return (error);
		}
#endif
		break;
	}
#endif
	default:
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EAFNOSUPPORT);
 		return (EAFNOSUPPORT);
	}
#endif
	SCTP_INP_INCR_REF(inp);
	SCTP_ASOC_CREATE_LOCK(inp);
	create_lock_on = 1;


	if ((inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE)) {
		/* Should I really unlock ? */
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EFAULT);
	        error = EFAULT;
		goto out_now;
	}
#ifdef INET6
	if (((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) == 0) &&
	    (addr->sa_family == AF_INET6)) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		error = EINVAL;
		goto out_now;
	}
#endif
#if defined(__Userspace__)
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_CONN) &&
	    (addr->sa_family != AF_CONN)) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		error = EINVAL;
		goto out_now;
	}
#endif
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND) ==
	    SCTP_PCB_FLAGS_UNBOUND) {
		/* Bind a ephemeral port */
		error = sctp_inpcb_bind(so, NULL, NULL, p);
		if (error) {
			goto out_now;
		}
	}
	/* Now do we connect? */
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) &&
	    (sctp_is_feature_off(inp, SCTP_PCB_FLAGS_PORTREUSE))) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		error = EINVAL;
		goto out_now;
	}
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) &&
	    (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED)) {
		/* We are already connected AND the TCP model */
		SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_USRREQ, EADDRINUSE);
		error = EADDRINUSE;
		goto out_now;
	}
	if (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) {
		SCTP_INP_RLOCK(inp);
		stcb = LIST_FIRST(&inp->sctp_asoc_list);
		SCTP_INP_RUNLOCK(inp);
	} else {
		/* We increment here since sctp_findassociation_ep_addr() will
		 * do a decrement if it finds the stcb as long as the locked
		 * tcb (last argument) is NOT a TCB.. aka NULL.
		 */
		SCTP_INP_INCR_REF(inp);
		stcb = sctp_findassociation_ep_addr(&inp, addr, NULL, NULL, NULL);
		if (stcb == NULL) {
			SCTP_INP_DECR_REF(inp);
		} else {
			SCTP_TCB_UNLOCK(stcb);
		}
	}
	if (stcb != NULL) {
		/* Already have or am bring up an association */
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EALREADY);
		error = EALREADY;
		goto out_now;
	}

	vrf_id = inp->def_vrf_id;
#ifdef SCTP_MVRF
	for (i = 0; i < inp->num_vrfs; i++) {
		if (vrf_id == inp->m_vrf_ids[i]) {
			fnd = 1;
			break;
		}
	}
	if (!fnd) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		error = EINVAL;
		goto out_now;
	}
#endif
	/* We are GOOD to go */
	stcb = sctp_aloc_assoc(inp, addr, &error, 0, vrf_id, p);
	if (stcb == NULL) {
		/* Gak! no memory */
		goto out_now;
	}
	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) {
		stcb->sctp_ep->sctp_flags |= SCTP_PCB_FLAGS_CONNECTED;
		/* Set the connected flag so we can queue data */
		soisconnecting(so);
	}
	SCTP_SET_STATE(&stcb->asoc, SCTP_STATE_COOKIE_WAIT);
	(void)SCTP_GETTIME_TIMEVAL(&stcb->asoc.time_entered);

	/* initialize authentication parameters for the assoc */
	sctp_initialize_auth_params(inp, stcb);

	sctp_send_initiate(inp, stcb, SCTP_SO_LOCKED);
	SCTP_TCB_UNLOCK(stcb);
 out_now:
	if (create_lock_on) {
		SCTP_ASOC_CREATE_UNLOCK(inp);
	}

	SCTP_INP_DECR_REF(inp);
	return (error);
}
#endif

#if defined(__Userspace__)
int
sctpconn_connect(struct socket *so, struct sockaddr *addr)
{
#ifdef SCTP_MVRF
	int i, fnd = 0;
#endif
	void *p = NULL;
	int error = 0;
	int create_lock_on = 0;
	uint32_t vrf_id;
	struct sctp_inpcb *inp;
	struct sctp_tcb *stcb = NULL;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		/* I made the same as TCP since we are not setup? */
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (ECONNRESET);
	}
	if (addr == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return EINVAL;
	}
	switch (addr->sa_family) {
#ifdef INET
	case AF_INET:
#ifdef HAVE_SA_LEN
		if (addr->sa_len != sizeof(struct sockaddr_in)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			return (EINVAL);
		}
#endif
		break;
#endif
#ifdef INET6
	case AF_INET6:
#ifdef HAVE_SA_LEN
		if (addr->sa_len != sizeof(struct sockaddr_in6)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			return (EINVAL);
		}
#endif
		break;
#endif
	case AF_CONN:
#ifdef HAVE_SA_LEN
		if (addr->sa_len != sizeof(struct sockaddr_conn)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			return (EINVAL);
		}
#endif
		break;
	default:
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EAFNOSUPPORT);
 		return (EAFNOSUPPORT);
	}
	SCTP_INP_INCR_REF(inp);
	SCTP_ASOC_CREATE_LOCK(inp);
	create_lock_on = 1;


	if ((inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE)) {
		/* Should I really unlock ? */
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EFAULT);
	        error = EFAULT;
		goto out_now;
	}
#ifdef INET6
	if (((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) == 0) &&
	    (addr->sa_family == AF_INET6)) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		error = EINVAL;
		goto out_now;
	}
#endif
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND) == SCTP_PCB_FLAGS_UNBOUND) {
		/* Bind a ephemeral port */
		error = sctp_inpcb_bind(so, NULL, NULL, p);
		if (error) {
			goto out_now;
		}
	}
	/* Now do we connect? */
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) &&
	    (sctp_is_feature_off(inp, SCTP_PCB_FLAGS_PORTREUSE))) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		error = EINVAL;
		goto out_now;
	}
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) &&
	    (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED)) {
		/* We are already connected AND the TCP model */
		SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_USRREQ, EADDRINUSE);
		error = EADDRINUSE;
		goto out_now;
	}
	if (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) {
		SCTP_INP_RLOCK(inp);
		stcb = LIST_FIRST(&inp->sctp_asoc_list);
		SCTP_INP_RUNLOCK(inp);
	} else {
		/* We increment here since sctp_findassociation_ep_addr() will
		 * do a decrement if it finds the stcb as long as the locked
		 * tcb (last argument) is NOT a TCB.. aka NULL.
		 */
		SCTP_INP_INCR_REF(inp);
		stcb = sctp_findassociation_ep_addr(&inp, addr, NULL, NULL, NULL);
		if (stcb == NULL) {
			SCTP_INP_DECR_REF(inp);
		} else {
			SCTP_TCB_UNLOCK(stcb);
		}
	}
	if (stcb != NULL) {
		/* Already have or am bring up an association */
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EALREADY);
		error = EALREADY;
		goto out_now;
	}

	vrf_id = inp->def_vrf_id;
#ifdef SCTP_MVRF
	for (i = 0; i < inp->num_vrfs; i++) {
		if (vrf_id == inp->m_vrf_ids[i]) {
			fnd = 1;
			break;
		}
	}
	if (!fnd) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		error = EINVAL;
		goto out_now;
	}
#endif
	/* We are GOOD to go */
	stcb = sctp_aloc_assoc(inp, addr, &error, 0, vrf_id, p);
	if (stcb == NULL) {
		/* Gak! no memory */
		goto out_now;
	}
	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) {
		stcb->sctp_ep->sctp_flags |= SCTP_PCB_FLAGS_CONNECTED;
		/* Set the connected flag so we can queue data */
		soisconnecting(so);
	}
	SCTP_SET_STATE(&stcb->asoc, SCTP_STATE_COOKIE_WAIT);
	(void)SCTP_GETTIME_TIMEVAL(&stcb->asoc.time_entered);

	/* initialize authentication parameters for the assoc */
	sctp_initialize_auth_params(inp, stcb);

	sctp_send_initiate(inp, stcb, SCTP_SO_LOCKED);
	SCTP_TCB_UNLOCK(stcb);
 out_now:
	if (create_lock_on) {
		SCTP_ASOC_CREATE_UNLOCK(inp);
	}

	SCTP_INP_DECR_REF(inp);
	return (error);
}
#endif
int
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
#if __FreeBSD_version >= 700000
sctp_listen(struct socket *so, int backlog, struct thread *p)
#else
sctp_listen(struct socket *so, struct thread *p)
#endif
#elif defined(__Windows__)
sctp_listen(struct socket *so, int backlog, PKTHREAD p)
#elif defined(__Userspace__)
sctp_listen(struct socket *so, int backlog, struct proc *p)
#else
sctp_listen(struct socket *so, struct proc *p)
#endif
{
	/*
	 * Note this module depends on the protocol processing being called
	 * AFTER any socket level flags and backlog are applied to the
	 * socket. The traditional way that the socket flags are applied is
	 * AFTER protocol processing. We have made a change to the
	 * sys/kern/uipc_socket.c module to reverse this but this MUST be in
	 * place if the socket API for SCTP is to work properly.
	 */

	int error = 0;
	struct sctp_inpcb *inp;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		/* I made the same as TCP since we are not setup? */
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (ECONNRESET);
	}
	if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_PORTREUSE)) {
		/* See if we have a listener */
		struct sctp_inpcb *tinp;
		union sctp_sockstore store, *sp;

		sp = &store;
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) == 0) {
			/* not bound all */
			struct sctp_laddr *laddr;

			LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
				memcpy(&store, &laddr->ifa->address, sizeof(store));
				switch (sp->sa.sa_family) {
#ifdef INET
				case AF_INET:
					sp->sin.sin_port = inp->sctp_lport;
					break;
#endif
#ifdef INET6
				case AF_INET6:
					sp->sin6.sin6_port = inp->sctp_lport;
					break;
#endif
#if defined(__Userspace__)
				case AF_CONN:
					sp->sconn.sconn_port = inp->sctp_lport;
					break;
#endif
				default:
					break;
				}
				tinp = sctp_pcb_findep(&sp->sa, 0, 0, inp->def_vrf_id);
				if (tinp && (tinp != inp) &&
				    ((tinp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) == 0) &&
				    ((tinp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) == 0) &&
				    (tinp->sctp_socket->so_qlimit)) {
					/* we have a listener already and its not this inp. */
					SCTP_INP_DECR_REF(tinp);
					return (EADDRINUSE);
				} else if (tinp) {
					SCTP_INP_DECR_REF(tinp);
				}
			}
		} else {
			/* Setup a local addr bound all */
			memset(&store, 0, sizeof(store));
			switch (sp->sa.sa_family) {
#ifdef INET
			case AF_INET:
				store.sin.sin_port = inp->sctp_lport;
				break;
#endif
#ifdef INET6
			case AF_INET6:
				sp->sin6.sin6_port = inp->sctp_lport;
				break;
#endif
#if defined(__Userspace__)
			case AF_CONN:
				sp->sconn.sconn_port = inp->sctp_lport;
				break;
#endif
			default:
				break;
			}
#ifdef INET6
			if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
				store.sa.sa_family = AF_INET6;
#ifdef HAVE_SA_LEN
				store.sa.sa_len = sizeof(struct sockaddr_in6);
#endif
			}
#endif
#ifdef INET
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) == 0) {
				store.sa.sa_family = AF_INET;
#ifdef HAVE_SA_LEN
				store.sa.sa_len = sizeof(struct sockaddr_in);
#endif
			}
#endif
			tinp = sctp_pcb_findep(&sp->sa, 0, 0, inp->def_vrf_id);
			if (tinp && (tinp != inp) &&
			    ((tinp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) == 0) &&
			    ((tinp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) == 0) &&
			    (tinp->sctp_socket->so_qlimit)) {
				/* we have a listener already and its not this inp. */
				SCTP_INP_DECR_REF(tinp);
				return (EADDRINUSE);
			} else if (tinp) {
				SCTP_INP_DECR_REF(inp);
			}
		}
	}
	SCTP_INP_RLOCK(inp);
#ifdef SCTP_LOCK_LOGGING
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOCK_LOGGING_ENABLE) {
		sctp_log_lock(inp, (struct sctp_tcb *)NULL, SCTP_LOG_LOCK_SOCK);
	}
#endif
	SOCK_LOCK(so);
#if (defined(__FreeBSD__) && __FreeBSD_version > 500000) || defined(__Userspace__)
	error = solisten_proto_check(so);
	if (error) {
		SOCK_UNLOCK(so);
		SCTP_INP_RUNLOCK(inp);
		return (error);
	}
#endif
	if ((sctp_is_feature_on(inp, SCTP_PCB_FLAGS_PORTREUSE)) &&
	    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) {
		/* The unlucky case
		 * - We are in the tcp pool with this guy.
		 * - Someone else is in the main inp slot.
		 * - We must move this guy (the listener) to the main slot
		 * - We must then move the guy that was listener to the TCP Pool.
		 */
		if (sctp_swap_inpcb_for_listen(inp)) {
			goto in_use;
		}
	}

	if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) &&
	    (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED)) {
		/* We are already connected AND the TCP model */
	in_use:
		SCTP_INP_RUNLOCK(inp);
		SOCK_UNLOCK(so);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EADDRINUSE);
		return (EADDRINUSE);
	}
	SCTP_INP_RUNLOCK(inp);
	if (inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND) {
		/* We must do a bind. */
		SOCK_UNLOCK(so);
		if ((error = sctp_inpcb_bind(so, NULL, NULL, p))) {
			/* bind error, probably perm */
			return (error);
		}
		SOCK_LOCK(so);
	}
#if (defined(__FreeBSD__) && __FreeBSD_version > 500000) || defined(__Windows__) || defined(__Userspace__)
#if __FreeBSD_version >= 700000 || defined(__Windows__) || defined(__Userspace__)
	/* It appears for 7.0 and on, we must always call this. */
	solisten_proto(so, backlog);
#else
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_UDPTYPE) == 0) {
		solisten_proto(so);
	}
#endif
#endif
	if (inp->sctp_flags & SCTP_PCB_FLAGS_UDPTYPE) {
		/* remove the ACCEPTCONN flag for one-to-many sockets */
#if defined(__Userspace__)
		so->so_options &= ~SCTP_SO_ACCEPTCONN;
#else
		so->so_options &= ~SO_ACCEPTCONN;
#endif
	}

#if __FreeBSD_version >= 700000 || defined(__Windows__) || defined(__Userspace__)
	if (backlog == 0) {
		/* turning off listen */
#if defined(__Userspace__)
		so->so_options &= ~SCTP_SO_ACCEPTCONN;
#else
		so->so_options &= ~SO_ACCEPTCONN;
#endif
	}
#endif
	SOCK_UNLOCK(so);
	return (error);
}

static int sctp_defered_wakeup_cnt = 0;

int
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__) || defined(__Userspace__)
sctp_accept(struct socket *so, struct sockaddr **addr)
{
#elif defined(__Panda__)
sctp_accept(struct socket *so, struct sockaddr *addr, int *namelen,
	    void *accept_info, int *accept_info_len)
{
#else
sctp_accept(struct socket *so, struct mbuf *nam)
{
	struct sockaddr *addr = mtod(nam, struct sockaddr *);
#endif
	struct sctp_tcb *stcb;
	struct sctp_inpcb *inp;
	union sctp_sockstore store;
#ifdef INET6
#ifdef SCTP_KAME
	int error;
#endif /* SCTP_KAME */
#endif
	inp = (struct sctp_inpcb *)so->so_pcb;

	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (ECONNRESET);
	}
	SCTP_INP_RLOCK(inp);
	if (inp->sctp_flags & SCTP_PCB_FLAGS_UDPTYPE) {
		SCTP_INP_RUNLOCK(inp);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EOPNOTSUPP);
		return (EOPNOTSUPP);
	}
	if (so->so_state & SS_ISDISCONNECTED) {
		SCTP_INP_RUNLOCK(inp);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ECONNABORTED);
		return (ECONNABORTED);
	}
	stcb = LIST_FIRST(&inp->sctp_asoc_list);
	if (stcb == NULL) {
		SCTP_INP_RUNLOCK(inp);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (ECONNRESET);
	}
	SCTP_TCB_LOCK(stcb);
	SCTP_INP_RUNLOCK(inp);
	store = stcb->asoc.primary_destination->ro._l_addr;
	stcb->asoc.state &= ~SCTP_STATE_IN_ACCEPT_QUEUE;
	SCTP_TCB_UNLOCK(stcb);
	switch (store.sa.sa_family) {
#ifdef INET
	case AF_INET:
	{
		struct sockaddr_in *sin;

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__) || defined(__Userspace__)
		SCTP_MALLOC_SONAME(sin, struct sockaddr_in *, sizeof *sin);
		if (sin == NULL)
			return (ENOMEM);
#else
		sin = (struct sockaddr_in *)addr;
		bzero((caddr_t)sin, sizeof(*sin));
#endif
		sin->sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
		sin->sin_len = sizeof(*sin);
#endif
		sin->sin_port = store.sin.sin_port;
		sin->sin_addr = store.sin.sin_addr;
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__) || defined(__Userspace__)
		*addr = (struct sockaddr *)sin;
#elif !defined(__Panda__)
		SCTP_BUF_LEN(nam) = sizeof(*sin);
#endif
		break;
	}
#endif
#ifdef INET6
	case AF_INET6:
	{
		struct sockaddr_in6 *sin6;

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__) || defined(__Userspace__)
		SCTP_MALLOC_SONAME(sin6, struct sockaddr_in6 *, sizeof *sin6);
		if (sin6 == NULL)
			return (ENOMEM);
#else
		sin6 = (struct sockaddr_in6 *)addr;
		bzero((caddr_t)sin6, sizeof(*sin6));
#endif
		sin6->sin6_family = AF_INET6;
#ifdef HAVE_SIN6_LEN
		sin6->sin6_len = sizeof(*sin6);
#endif
		sin6->sin6_port = store.sin6.sin6_port;
		sin6->sin6_addr = store.sin6.sin6_addr;
#if defined(SCTP_EMBEDDED_V6_SCOPE)
#ifdef SCTP_KAME
		if ((error = sa6_recoverscope(sin6)) != 0) {
			SCTP_FREE_SONAME(sin6);
			return (error);
		}
#else
		if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr))
			/*
			 * sin6->sin6_scope_id =
			 * ntohs(sin6->sin6_addr.s6_addr16[1]);
			 */
			in6_recoverscope(sin6, &sin6->sin6_addr, NULL);	/* skip ifp check */
		else
			sin6->sin6_scope_id = 0;	/* XXX */
#endif /* SCTP_KAME */
#endif /* SCTP_EMBEDDED_V6_SCOPE */
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__) || defined(__Userspace__)
		*addr = (struct sockaddr *)sin6;
#elif !defined(__Panda__)
		SCTP_BUF_LEN(nam) = sizeof(*sin6);
#endif
		break;
	}
#endif
#if defined(__Userspace__)
	case AF_CONN:
	{
		struct sockaddr_conn *sconn;

		SCTP_MALLOC_SONAME(sconn, struct sockaddr_conn *, sizeof(struct sockaddr_conn));
		if (sconn == NULL) {
			return (ENOMEM);
		}
		sconn->sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
		sconn->sconn_len = sizeof(struct sockaddr_conn);
#endif
		sconn->sconn_port = store.sconn.sconn_port;
		sconn->sconn_addr = store.sconn.sconn_addr;
		*addr = (struct sockaddr *)sconn;
		break;
	}
#endif
	default:
		/* TSNH */
		break;
	}
	/* Wake any delayed sleep action */
	if (inp->sctp_flags & SCTP_PCB_FLAGS_DONT_WAKE) {
		SCTP_INP_WLOCK(inp);
		inp->sctp_flags &= ~SCTP_PCB_FLAGS_DONT_WAKE;
		if (inp->sctp_flags & SCTP_PCB_FLAGS_WAKEOUTPUT) {
			inp->sctp_flags &= ~SCTP_PCB_FLAGS_WAKEOUTPUT;
			SCTP_INP_WUNLOCK(inp);
			SOCKBUF_LOCK(&inp->sctp_socket->so_snd);
			if (sowriteable(inp->sctp_socket)) {
#if defined(__Userspace__)
                            /*__Userspace__ calling sowwakup_locked because of SOCKBUF_LOCK above. */
#endif
#if defined(__FreeBSD__) || defined(__Windows__) || defined(__Userspace__)
				sowwakeup_locked(inp->sctp_socket);
#else
#if defined(__APPLE__)
				/* socket is locked */
#endif
				sowwakeup(inp->sctp_socket);
#endif
			} else {
				SOCKBUF_UNLOCK(&inp->sctp_socket->so_snd);
			}
			SCTP_INP_WLOCK(inp);
		}
		if (inp->sctp_flags & SCTP_PCB_FLAGS_WAKEINPUT) {
			inp->sctp_flags &= ~SCTP_PCB_FLAGS_WAKEINPUT;
			SCTP_INP_WUNLOCK(inp);
			SOCKBUF_LOCK(&inp->sctp_socket->so_rcv);
			if (soreadable(inp->sctp_socket)) {
				sctp_defered_wakeup_cnt++;
#if defined(__Userspace__)
                                /*__Userspace__ calling sorwakup_locked because of SOCKBUF_LOCK above */
#endif
#if defined(__FreeBSD__) || defined(__Windows__) || defined(__Userspace__)
				sorwakeup_locked(inp->sctp_socket);
#else
#if defined(__APPLE__)
				/* socket is locked */
#endif
				sorwakeup(inp->sctp_socket);
#endif
			} else {
				SOCKBUF_UNLOCK(&inp->sctp_socket->so_rcv);
			}
			SCTP_INP_WLOCK(inp);
		}
		SCTP_INP_WUNLOCK(inp);
	}
	if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
		SCTP_TCB_LOCK(stcb);
		sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC, SCTP_FROM_SCTP_USRREQ+SCTP_LOC_7);
	}
	return (0);
}

#ifdef INET
int
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
sctp_ingetaddr(struct socket *so, struct sockaddr **addr)
{
	struct sockaddr_in *sin;
#elif defined(__Panda__)
sctp_ingetaddr(struct socket *so, struct sockaddr *addr)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)addr;
#else
sctp_ingetaddr(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_in *sin = mtod(nam, struct sockaddr_in *);
#endif
	uint32_t vrf_id;
	struct sctp_inpcb *inp;
	struct sctp_ifa *sctp_ifa;

	/*
	 * Do the malloc first in case it blocks.
	 */
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
	SCTP_MALLOC_SONAME(sin, struct sockaddr_in *, sizeof *sin);
	if (sin == NULL)
		return (ENOMEM);
#elif defined(__Panda__)
	bzero(sin, sizeof(*sin));
#else
	SCTP_BUF_LEN(nam) = sizeof(*sin);
	memset(sin, 0, sizeof(*sin));
#endif
	sin->sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
	sin->sin_len = sizeof(*sin);
#endif
	inp = (struct sctp_inpcb *)so->so_pcb;
	if (!inp) {
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
		SCTP_FREE_SONAME(sin);
#endif
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (ECONNRESET);
	}
	SCTP_INP_RLOCK(inp);
	sin->sin_port = inp->sctp_lport;
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		if (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) {
			struct sctp_tcb *stcb;
			struct sockaddr_in *sin_a;
			struct sctp_nets *net;
			int fnd;

			stcb = LIST_FIRST(&inp->sctp_asoc_list);
			if (stcb == NULL) {
				goto notConn;
			}
			fnd = 0;
			sin_a = NULL;
			SCTP_TCB_LOCK(stcb);
			TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
				sin_a = (struct sockaddr_in *)&net->ro._l_addr;
				if (sin_a == NULL)
					/* this will make coverity happy */
					continue;

				if (sin_a->sin_family == AF_INET) {
					fnd = 1;
					break;
				}
			}
			if ((!fnd) || (sin_a == NULL)) {
				/* punt */
				SCTP_TCB_UNLOCK(stcb);
				goto notConn;
			}

			vrf_id = inp->def_vrf_id;
			sctp_ifa = sctp_source_address_selection(inp,
								 stcb,
								 (sctp_route_t *)&net->ro,
								 net, 0, vrf_id);
			if (sctp_ifa) {
				sin->sin_addr = sctp_ifa->address.sin.sin_addr;
				sctp_free_ifa(sctp_ifa);
			}
			SCTP_TCB_UNLOCK(stcb);
		} else {
			/* For the bound all case you get back 0 */
	notConn:
			sin->sin_addr.s_addr = 0;
		}

	} else {
		/* Take the first IPv4 address in the list */
		struct sctp_laddr *laddr;
		int fnd = 0;

		LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
			if (laddr->ifa->address.sa.sa_family == AF_INET) {
				struct sockaddr_in *sin_a;

				sin_a = (struct sockaddr_in *)&laddr->ifa->address.sa;
				sin->sin_addr = sin_a->sin_addr;
				fnd = 1;
				break;
			}
		}
		if (!fnd) {
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
			SCTP_FREE_SONAME(sin);
#endif
			SCTP_INP_RUNLOCK(inp);
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOENT);
			return (ENOENT);
		}
	}
	SCTP_INP_RUNLOCK(inp);
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
	(*addr) = (struct sockaddr *)sin;
#endif
	return (0);
}

int
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
sctp_peeraddr(struct socket *so, struct sockaddr **addr)
{
	struct sockaddr_in *sin;
#elif defined(__Panda__)
sctp_peeraddr(struct socket *so, struct sockaddr *addr)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)addr;
#else
sctp_peeraddr(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_in *sin = mtod(nam, struct sockaddr_in *);

#endif
	int fnd;
	struct sockaddr_in *sin_a;
	struct sctp_inpcb *inp;
	struct sctp_tcb *stcb;
	struct sctp_nets *net;

	/* Do the malloc first in case it blocks. */
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
	SCTP_MALLOC_SONAME(sin, struct sockaddr_in *, sizeof *sin);
	if (sin == NULL)
		return (ENOMEM);
#elif defined(__Panda__)
	memset(sin, 0, sizeof(*sin));
#else
	SCTP_BUF_LEN(nam) = sizeof(*sin);
	memset(sin, 0, sizeof(*sin));
#endif
	sin->sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
	sin->sin_len = sizeof(*sin);
#endif

	inp = (struct sctp_inpcb *)so->so_pcb;
	if ((inp == NULL) ||
	    ((inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) == 0)) {
		/* UDP type and listeners will drop out here */
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
		SCTP_FREE_SONAME(sin);
#endif
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOTCONN);
		return (ENOTCONN);
	}
	SCTP_INP_RLOCK(inp);
	stcb = LIST_FIRST(&inp->sctp_asoc_list);
	if (stcb) {
		SCTP_TCB_LOCK(stcb);
	}
	SCTP_INP_RUNLOCK(inp);
	if (stcb == NULL) {
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
		SCTP_FREE_SONAME(sin);
#endif
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
		return (ECONNRESET);
	}
	fnd = 0;
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		sin_a = (struct sockaddr_in *)&net->ro._l_addr;
		if (sin_a->sin_family == AF_INET) {
			fnd = 1;
			sin->sin_port = stcb->rport;
			sin->sin_addr = sin_a->sin_addr;
			break;
		}
	}
	SCTP_TCB_UNLOCK(stcb);
	if (!fnd) {
		/* No IPv4 address */
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
		SCTP_FREE_SONAME(sin);
#endif
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, ENOENT);
		return (ENOENT);
	}
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
	(*addr) = (struct sockaddr *)sin;
#endif
	return (0);
}

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Windows__)
struct pr_usrreqs sctp_usrreqs = {
#if __FreeBSD_version >= 600000
	.pru_abort = sctp_abort,
	.pru_accept = sctp_accept,
	.pru_attach = sctp_attach,
	.pru_bind = sctp_bind,
	.pru_connect = sctp_connect,
	.pru_control = in_control,
#if __FreeBSD_version >= 690000
	.pru_close = sctp_close,
	.pru_detach = sctp_close,
	.pru_sopoll = sopoll_generic,
	.pru_flush = sctp_flush,
#else
	.pru_detach = sctp_detach,
	.pru_sopoll = sopoll,
#endif
	.pru_disconnect = sctp_disconnect,
	.pru_listen = sctp_listen,
	.pru_peeraddr = sctp_peeraddr,
	.pru_send = sctp_sendm,
	.pru_shutdown = sctp_shutdown,
	.pru_sockaddr = sctp_ingetaddr,
	.pru_sosend = sctp_sosend,
	.pru_soreceive = sctp_soreceive
#else
	sctp_abort,
	sctp_accept,
	sctp_attach,
	sctp_bind,
	sctp_connect,
	pru_connect2_notsupp,
#if defined(__Windows__)
	NULL,
	NULL,
#else
	in_control,
	sctp_detach,
#endif
	sctp_disconnect,
	sctp_listen,
	sctp_peeraddr,
	NULL,
	pru_rcvoob_notsupp,
#if defined(__Windows__)
	NULL,
#else
	sctp_sendm,
#endif
	pru_sense_null,
	sctp_shutdown,
#if defined(__Windows__)
	sctp_flush,
#endif
	sctp_ingetaddr,
	sctp_sosend,
	sctp_soreceive,
#if defined(__Windows__)
	sopoll_generic,
	NULL,
	sctp_close
#else
	sopoll
#endif
#endif
};
#elif !defined(__Panda__) && !defined(__Userspace__)
int
sctp_usrreq(so, req, m, nam, control)
	struct socket *so;
	int req;
	struct mbuf *m, *nam, *control;
{
	struct proc *p = curproc;
	uint32_t vrf_id;
	struct sctp_vrf *vrf;
	int error;
	int family;
	struct sctp_inpcb *inp = (struct sctp_inpcb *)so->so_pcb;

	error = 0;
	family = so->so_proto->pr_domain->dom_family;
	if (req == PRU_CONTROL) {
		switch (family) {
		case PF_INET:
			error = in_control(so, (long)m, (caddr_t)nam,
			    (struct ifnet *)control);
			break;
#ifdef INET6
		case PF_INET6:
			error = in6_control(so, (long)m, (caddr_t)nam,
			    (struct ifnet *)control, p);
			break;
#endif
		default:
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EAFNOSUPPORT);
			error = EAFNOSUPPORT;
		}
		return (error);
	}
	switch (req) {
	case PRU_ATTACH:
		error = sctp_attach(so, family, p);
		break;
	case PRU_DETACH:
		error = sctp_detach(so);
		break;
	case PRU_BIND:
		if (nam == NULL) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			return (EINVAL);
		}
		error = sctp_bind(so, nam, p);
		break;
	case PRU_LISTEN:
		error = sctp_listen(so, p);
		break;
	case PRU_CONNECT:
		if (nam == NULL) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			return (EINVAL);
		}
		error = sctp_connect(so, nam, p);
		break;
	case PRU_DISCONNECT:
		error = sctp_disconnect(so);
		break;
	case PRU_ACCEPT:
		if (nam == NULL) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EINVAL);
			return (EINVAL);
		}
		error = sctp_accept(so, nam);
		break;
	case PRU_SHUTDOWN:
		error = sctp_shutdown(so);
		break;

	case PRU_RCVD:
		/*
		 * For Open and Net BSD, this is real ugly. The mbuf *nam
		 * that is passed (by soreceive()) is the int flags c ast as
		 * a (mbuf *) yuck!
		 */
		break;

	case PRU_SEND:
		/* Flags are ignored */
		{
			struct sockaddr *addr;

			if (nam == NULL)
				addr = NULL;
			else
				addr = mtod(nam, struct sockaddr *);

			error = sctp_sendm(so, 0, m, addr, control, p);
		}
		break;
	case PRU_ABORT:
		error = sctp_abort(so);
		break;

	case PRU_SENSE:
		error = 0;
		break;
	case PRU_RCVOOB:
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EAFNOSUPPORT);
		error = EAFNOSUPPORT;
		break;
	case PRU_SENDOOB:
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_USRREQ, EAFNOSUPPORT);
		error = EAFNOSUPPORT;
		break;
	case PRU_PEERADDR:
		error = sctp_peeraddr(so, nam);
		break;
	case PRU_SOCKADDR:
		error = sctp_ingetaddr(so, nam);
		break;
	case PRU_SLOWTIMO:
		error = 0;
		break;
	default:
		break;
	}
	return (error);
}

#endif
#endif

#if defined(__Userspace__)
int
register_recv_cb(struct socket *so,
                 int (*receive_cb)(struct socket *sock, union sctp_sockstore addr, void *data,
                 size_t datalen, struct sctp_rcvinfo, int flags, void *ulp_info))
{
	struct sctp_inpcb *inp;

	inp = (struct sctp_inpcb *) so->so_pcb;
	if (inp == NULL) {
		return (0);
	}
	SCTP_INP_WLOCK(inp);
	inp->recv_callback = receive_cb;
	SCTP_INP_WUNLOCK(inp);
	return (1);
}

int
register_send_cb(struct socket *so, uint32_t sb_threshold, int (*send_cb)(struct socket *sock, uint32_t sb_free))
{
	struct sctp_inpcb *inp;

	inp = (struct sctp_inpcb *) so->so_pcb;
	if (inp == NULL) {
		return (0);
	}
	SCTP_INP_WLOCK(inp);
	inp->send_callback = send_cb;
	inp->send_sb_threshold = sb_threshold;
	SCTP_INP_WUNLOCK(inp);
	/* FIXME change to current amount free. This will be the full buffer
	 * the first time this is registered but it could be only a portion
	 * of the send buffer if this is called a second time e.g. if the
	 * threshold changes.
	 */
	return (1);
}

int
register_ulp_info (struct socket *so, void *ulp_info)
{
	struct sctp_inpcb *inp;

	inp = (struct sctp_inpcb *) so->so_pcb;
	if (inp == NULL) {
		return (0);
	}
	SCTP_INP_WLOCK(inp);
	inp->ulp_info = ulp_info;
	SCTP_INP_WUNLOCK(inp);
	return (1);
}
#endif

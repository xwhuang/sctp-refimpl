#define SCTP_SLEEP_MS	100
void sctp_delay(int ms);

int sctp_one2one(unsigned short port, int should_listen, int bindall);
int sctp_socketpair(int *, int bindall);
int sctp_socketpair_reuse(int fd, int *fds, int bindall);
int sctp_socketstar(int *, int *, unsigned int);
int sctp_shutdown(int);
int sctp_abort(int);
int sctp_enable_non_blocking(int);
int sctp_disable_non_blocking_blocking(int);
int sctp_set_rto_info(int, sctp_assoc_t, uint32_t, uint32_t, uint32_t);
int sctp_set_initial_rto(int , sctp_assoc_t, uint32_t);
int sctp_set_maximum_rto(int , sctp_assoc_t, uint32_t);
int sctp_set_minimum_rto(int , sctp_assoc_t, uint32_t);
int sctp_get_rto_info(int, sctp_assoc_t, uint32_t *, uint32_t *, uint32_t *);
int sctp_get_initial_rto(int fd, sctp_assoc_t, uint32_t *);
int sctp_get_maximum_rto(int fd, sctp_assoc_t, uint32_t *);
int sctp_get_minimum_rto(int fd, sctp_assoc_t, uint32_t *);

int sctp_one2many(unsigned short port, int bindall);
int sctp_socketpair_1tom(int *fds, sctp_assoc_t *asocids, int bindall);
int sctp_get_assoc_info(int fd, sctp_assoc_t assoc_id, 
			uint16_t *asoc_maxrxt,
			uint16_t *peer_dest_cnt, 
			uint32_t *peer_rwnd,
			uint32_t *local_rwnd,
			uint32_t *cookie_life);
int sctp_set_assoc_info(int fd, sctp_assoc_t assoc_id, 
			uint16_t asoc_maxrxt,
			uint16_t peer_dest_cnt, 
			uint32_t peer_rwnd,
			uint32_t local_rwnd,
			uint32_t cookie_life);
int sctp_set_asoc_maxrxt(int fd, sctp_assoc_t asoc, uint16_t max);
int sctp_get_asoc_maxrxt(int fd, sctp_assoc_t asoc, uint16_t *max);

int sctp_set_asoc_peerdest_cnt(int fd, sctp_assoc_t asoc, uint16_t dstcnt);
int sctp_get_asoc_peerdest_cnt(int fd, sctp_assoc_t asoc, uint16_t *dst);

int sctp_set_asoc_peer_rwnd(int fd, sctp_assoc_t asoc, uint32_t rwnd);
int sctp_get_asoc_peer_rwnd(int fd, sctp_assoc_t asoc, uint32_t *rwnd);

int sctp_set_asoc_local_rwnd(int fd, sctp_assoc_t asoc, uint32_t lrwnd);
int sctp_get_asoc_local_rwnd(int fd, sctp_assoc_t asoc, uint32_t *lrwnd);

int sctp_set_asoc_cookie_life(int fd, sctp_assoc_t asoc, uint32_t life);
int sctp_get_asoc_cookie_life(int fd, sctp_assoc_t asoc, uint32_t *life);

uint32_t sctp_get_number_of_associations(int);
uint32_t sctp_get_association_identifiers(int, sctp_assoc_t [], unsigned int);

int 
sctp_get_initmsg(int fd, 
		 uint32_t *ostreams,
		 uint32_t *istreams,
		 uint16_t *maxattempt,
		 uint16_t *max_init_timeo);

int 
sctp_set_initmsg(int fd, 
		 uint32_t ostreams,
		 uint32_t istreams,
		 uint16_t maxattempt,
		 uint16_t max_init_timeo);

int sctp_set_im_ostream(int fd, uint32_t ostream);
int sctp_set_im_istream(int fd, uint32_t istream);
int sctp_set_im_maxattempt(int fd, uint16_t max);
int sctp_set_im_maxtimeo(int fd, uint16_t timeo);
int sctp_get_ndelay(int fd, uint32_t *val);
int sctp_set_ndelay(int fd, uint32_t val);
int sctp_get_autoclose(int fd, uint32_t *val);
int sctp_set_autoclose(int fd, uint32_t val);

int sctp_set_peer_prim(int, sctp_assoc_t,  struct sockaddr *);


int sctp_set_primary(int, sctp_assoc_t, struct sockaddr *);
int sctp_get_primary(int, sctp_assoc_t, struct sockaddr *, socklen_t *len);

int sctp_set_adaptation( int fd, uint32_t val);
int sctp_get_adaptation( int fd, uint32_t *val);

int sctp_set_disfrag( int fd, int val);
int sctp_get_disfrag( int fd, int *val);

int sctp_get_paddr_param(int fd, sctp_assoc_t id, 
			 struct sockaddr *sa,
			 uint32_t *hbinterval,
			 uint16_t *maxrxt,
			 uint32_t *pathmtu,
			 uint32_t *flags,
			 uint32_t *ipv6_flowlabel,
			 uint8_t *ipv4_tos);

int sctp_set_paddr_param(int fd, sctp_assoc_t id, 
			 struct sockaddr *sa,
			 uint32_t hbinterval,
			 uint16_t maxrxt,
			 uint32_t pathmtu,
			 uint32_t flags,
			 uint32_t ipv6_flowlabel,
			 uint8_t ipv4_tos);



int
sctp_set_hbint(int fd, sctp_assoc_t id, 
	       struct sockaddr *sa,
	       uint16_t hbinterval);
int
sctp_set_hbdisable(int fd, sctp_assoc_t id, 
		   struct sockaddr *sa);

int
sctp_set_hbenable(int fd, sctp_assoc_t id, 
		   struct sockaddr *sa);

int
sctp_set_hbzero(int fd, sctp_assoc_t id, 
		struct sockaddr *sa);


int
sctp_set_maxrxt(int fd, sctp_assoc_t id, 
		struct sockaddr *sa,
		uint32_t maxrxt);

int
sctp_set_pmtu(int fd, sctp_assoc_t id, 
	      struct sockaddr *sa,
	      uint32_t pathmtu);
int
sctp_set_pmtu_enable(int fd, sctp_assoc_t id, 
		     struct sockaddr *sa);

int
sctp_set_flow(int fd, sctp_assoc_t id, 
	      struct sockaddr *sa,
	      uint32_t ipv6_flowlabel);

int
sctp_set_tos(int fd, sctp_assoc_t id, 
	     struct sockaddr *sa,
	     uint8_t ipv4_tos);

int sctp_get_defsend(int fd, sctp_assoc_t id, struct sctp_sndrcvinfo *s);
int sctp_set_defsend(int fd, sctp_assoc_t id, struct sctp_sndrcvinfo *s);


int sctp_set_maxseg(int fd, sctp_assoc_t id, int val);
int sctp_get_maxseg(int fd, sctp_assoc_t id, int *val);

int sctp_set_events(int fd, struct sctp_event_subscribe *ev);
int sctp_get_events(int fd, struct sctp_event_subscribe *ev);

int sctp_get_auth_chunk_id(int fd, uint8_t *fill);
int sctp_set_auth_chunk_id(int fd, uint8_t chk);

/********************************************************
 *
 * SCTP_KEY tests
 *
 ********************************************************/
int sctp_get_add_key(int fd, sctp_assoc_t assoc_id, uint16_t keyid,
		     uint16_t *keylen, uint8_t *keytext);
int sctp_set_add_key(int fd, sctp_assoc_t assoc_id, uint16_t keyid,
		     uint16_t keylen, uint8_t *keytext);

int sctp_get_active_key(int fd, sctp_assoc_t assoc_id, uint16_t *keyid);
int sctp_set_active_key(int fd, sctp_assoc_t assoc_id, uint16_t keyid);

int sctp_get_delete_key(int fd, sctp_assoc_t assoc_id, uint16_t *keyid);
int sctp_set_delete_key(int fd, sctp_assoc_t assoc_id, uint16_t keyid);

/********************************************************
 *
 * SCTP_OTHER tests
 *
 ********************************************************/


/********************************************************
 *
 * SCTP_DELAYED_SACK tests
 *
 ********************************************************/
int sctp_set_dsack(int fd, sctp_assoc_t id, uint32_t delay, uint32_t freq);
int sctp_get_dsack(int fd, sctp_assoc_t id,uint32_t *delay, uint32_t *freq);
int sctp_set_ddelay(int fd, sctp_assoc_t id, uint32_t delay);
int sctp_set_dfreq(int fd, sctp_assoc_t id, uint32_t freq);



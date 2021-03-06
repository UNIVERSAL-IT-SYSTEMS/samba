/*
   Unix SMB/CIFS implementation.
   Samba utility functions
   Copyright (C) Andrew Tridgell 1992-1998
   Copyright (C) Tim Potter      2000-2001
   Copyright (C) Jeremy Allison  1992-2007

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"

/*******************************************************************
 Map a text hostname or IP address (IPv4 or IPv6) into a
 struct sockaddr_storage.
******************************************************************/

bool interpret_string_addr(struct sockaddr_storage *pss,
		const char *str,
		int flags)
{
	struct addrinfo *res = NULL;
#if defined(HAVE_IPV6)
	char addr[INET6_ADDRSTRLEN];
	unsigned int scope_id = 0;

	if (strchr_m(str, ':')) {
		char *p = strchr_m(str, '%');

		/*
		 * Cope with link-local.
		 * This is IP:v6:addr%ifname.
		 */

		if (p && (p > str) && ((scope_id = if_nametoindex(p+1)) != 0)) {
			strlcpy(addr, str,
				MIN(PTR_DIFF(p,str)+1,
					sizeof(addr)));
			str = addr;
		}
	}
#endif

	zero_sockaddr(pss);

	if (!interpret_string_addr_internal(&res, str, flags|AI_ADDRCONFIG)) {
		return false;
	}
	if (!res) {
		return false;
	}
	/* Copy the first sockaddr. */
	memcpy(pss, res->ai_addr, res->ai_addrlen);

#if defined(HAVE_IPV6)
	if (pss->ss_family == AF_INET6 && scope_id) {
		struct sockaddr_in6 *ps6 = (struct sockaddr_in6 *)pss;
		if (IN6_IS_ADDR_LINKLOCAL(&ps6->sin6_addr) &&
				ps6->sin6_scope_id == 0) {
			ps6->sin6_scope_id = scope_id;
		}
	}
#endif

	freeaddrinfo(res);
	return true;
}

/*******************************************************************
 Set an address to INADDR_ANY.
******************************************************************/

void zero_sockaddr(struct sockaddr_storage *pss)
{
	memset(pss, '\0', sizeof(*pss));
	/* Ensure we're at least a valid sockaddr-storage. */
	pss->ss_family = AF_INET;
}

/****************************************************************************
 Get a port number in host byte order from a sockaddr_storage.
****************************************************************************/

uint16_t get_sockaddr_port(const struct sockaddr_storage *pss)
{
	uint16_t port = 0;

	if (pss->ss_family != AF_INET) {
#if defined(HAVE_IPV6)
		/* IPv6 */
		const struct sockaddr_in6 *sa6 =
			(const struct sockaddr_in6 *)pss;
		port = ntohs(sa6->sin6_port);
#endif
	} else {
		const struct sockaddr_in *sa =
			(const struct sockaddr_in *)pss;
		port = ntohs(sa->sin_port);
	}
	return port;
}

/****************************************************************************
 Print out an IPv4 or IPv6 address from a struct sockaddr_storage.
****************************************************************************/

static char *print_sockaddr_len(char *dest,
			size_t destlen,
			const struct sockaddr *psa,
			socklen_t psalen)
{
	if (destlen > 0) {
		dest[0] = '\0';
	}
	(void)sys_getnameinfo(psa,
			psalen,
			dest, destlen,
			NULL, 0,
			NI_NUMERICHOST);
	return dest;
}

/****************************************************************************
 Print out an IPv4 or IPv6 address from a struct sockaddr_storage.
****************************************************************************/

char *print_sockaddr(char *dest,
			size_t destlen,
			const struct sockaddr_storage *psa)
{
	return print_sockaddr_len(dest, destlen, (struct sockaddr *)psa,
			sizeof(struct sockaddr_storage));
}

/****************************************************************************
 Print out a canonical IPv4 or IPv6 address from a struct sockaddr_storage.
****************************************************************************/

char *print_canonical_sockaddr(TALLOC_CTX *ctx,
			const struct sockaddr_storage *pss)
{
	char addr[INET6_ADDRSTRLEN];
	char *dest = NULL;
	int ret;

	/* Linux getnameinfo() man pages says port is unitialized if
	   service name is NULL. */

	ret = sys_getnameinfo((const struct sockaddr *)pss,
			sizeof(struct sockaddr_storage),
			addr, sizeof(addr),
			NULL, 0,
			NI_NUMERICHOST);
	if (ret != 0) {
		return NULL;
	}

	if (pss->ss_family != AF_INET) {
#if defined(HAVE_IPV6)
		dest = talloc_asprintf(ctx, "[%s]", addr);
#else
		return NULL;
#endif
	} else {
		dest = talloc_asprintf(ctx, "%s", addr);
	}
	
	return dest;
}

/****************************************************************************
 Return the string of an IP address (IPv4 or IPv6).
****************************************************************************/

static const char *get_socket_addr(int fd, char *addr_buf, size_t addr_len)
{
	struct sockaddr_storage sa;
	socklen_t length = sizeof(sa);

	/* Ok, returning a hard coded IPv4 address
 	 * is bogus, but it's just as bogus as a
 	 * zero IPv6 address. No good choice here.
 	 */

	strlcpy(addr_buf, "0.0.0.0", addr_len);

	if (fd == -1) {
		return addr_buf;
	}

	if (getsockname(fd, (struct sockaddr *)&sa, &length) < 0) {
		DEBUG(0,("getsockname failed. Error was %s\n",
			strerror(errno) ));
		return addr_buf;
	}

	return print_sockaddr_len(addr_buf, addr_len, (struct sockaddr *)&sa, length);
}

/****************************************************************************
 Return the port number we've bound to on a socket.
****************************************************************************/

int get_socket_port(int fd)
{
	struct sockaddr_storage sa;
	socklen_t length = sizeof(sa);

	if (fd == -1) {
		return -1;
	}

	if (getsockname(fd, (struct sockaddr *)&sa, &length) < 0) {
		DEBUG(0,("getpeername failed. Error was %s\n",
			strerror(errno) ));
		return -1;
	}

#if defined(HAVE_IPV6)
	if (sa.ss_family == AF_INET6) {
		return ntohs(((struct sockaddr_in6 *)&sa)->sin6_port);
	}
#endif
	if (sa.ss_family == AF_INET) {
		return ntohs(((struct sockaddr_in *)&sa)->sin_port);
	}
	return -1;
}

const char *client_name(int fd)
{
	return get_peer_name(fd,false);
}

const char *client_addr(int fd, char *addr, size_t addrlen)
{
	return get_peer_addr(fd,addr,addrlen);
}

const char *client_socket_addr(int fd, char *addr, size_t addr_len)
{
	return get_socket_addr(fd, addr, addr_len);
}

#if 0
/* Not currently used. JRA. */
int client_socket_port(int fd)
{
	return get_socket_port(fd);
}
#endif

/****************************************************************************
 Accessor functions to make thread-safe code easier later...
****************************************************************************/

void set_smb_read_error(enum smb_read_errors *pre,
			enum smb_read_errors newerr)
{
	if (pre) {
		*pre = newerr;
	}
}

void cond_set_smb_read_error(enum smb_read_errors *pre,
			enum smb_read_errors newerr)
{
	if (pre && *pre == SMB_READ_OK) {
		*pre = newerr;
	}
}

/****************************************************************************
 Determine if a file descriptor is in fact a socket.
****************************************************************************/

bool is_a_socket(int fd)
{
	int v;
	socklen_t l;
	l = sizeof(int);
	return(getsockopt(fd, SOL_SOCKET, SO_TYPE, (char *)&v, &l) == 0);
}

enum SOCK_OPT_TYPES {OPT_BOOL,OPT_INT,OPT_ON};

typedef struct smb_socket_option {
	const char *name;
	int level;
	int option;
	int value;
	int opttype;
} smb_socket_option;

static const smb_socket_option socket_options[] = {
  {"SO_KEEPALIVE", SOL_SOCKET, SO_KEEPALIVE, 0, OPT_BOOL},
  {"SO_REUSEADDR", SOL_SOCKET, SO_REUSEADDR, 0, OPT_BOOL},
  {"SO_BROADCAST", SOL_SOCKET, SO_BROADCAST, 0, OPT_BOOL},
#ifdef TCP_NODELAY
  {"TCP_NODELAY", IPPROTO_TCP, TCP_NODELAY, 0, OPT_BOOL},
#endif
#ifdef TCP_KEEPCNT
  {"TCP_KEEPCNT", IPPROTO_TCP, TCP_KEEPCNT, 0, OPT_INT},
#endif
#ifdef TCP_KEEPIDLE
  {"TCP_KEEPIDLE", IPPROTO_TCP, TCP_KEEPIDLE, 0, OPT_INT},
#endif
#ifdef TCP_KEEPINTVL
  {"TCP_KEEPINTVL", IPPROTO_TCP, TCP_KEEPINTVL, 0, OPT_INT},
#endif
#ifdef IPTOS_LOWDELAY
  {"IPTOS_LOWDELAY", IPPROTO_IP, IP_TOS, IPTOS_LOWDELAY, OPT_ON},
#endif
#ifdef IPTOS_THROUGHPUT
  {"IPTOS_THROUGHPUT", IPPROTO_IP, IP_TOS, IPTOS_THROUGHPUT, OPT_ON},
#endif
#ifdef SO_REUSEPORT
  {"SO_REUSEPORT", SOL_SOCKET, SO_REUSEPORT, 0, OPT_BOOL},
#endif
#ifdef SO_SNDBUF
  {"SO_SNDBUF", SOL_SOCKET, SO_SNDBUF, 0, OPT_INT},
#endif
#ifdef SO_RCVBUF
  {"SO_RCVBUF", SOL_SOCKET, SO_RCVBUF, 0, OPT_INT},
#endif
#ifdef SO_SNDLOWAT
  {"SO_SNDLOWAT", SOL_SOCKET, SO_SNDLOWAT, 0, OPT_INT},
#endif
#ifdef SO_RCVLOWAT
  {"SO_RCVLOWAT", SOL_SOCKET, SO_RCVLOWAT, 0, OPT_INT},
#endif
#ifdef SO_SNDTIMEO
  {"SO_SNDTIMEO", SOL_SOCKET, SO_SNDTIMEO, 0, OPT_INT},
#endif
#ifdef SO_RCVTIMEO
  {"SO_RCVTIMEO", SOL_SOCKET, SO_RCVTIMEO, 0, OPT_INT},
#endif
#ifdef TCP_FASTACK
  {"TCP_FASTACK", IPPROTO_TCP, TCP_FASTACK, 0, OPT_INT},
#endif
  {NULL,0,0,0,0}};

/****************************************************************************
 Print socket options.
****************************************************************************/

static void print_socket_options(int s)
{
	int value;
	socklen_t vlen = 4;
	const smb_socket_option *p = &socket_options[0];

	/* wrapped in if statement to prevent streams
	 * leak in SCO Openserver 5.0 */
	/* reported on samba-technical  --jerry */
	if ( DEBUGLEVEL >= 5 ) {
		DEBUG(5,("Socket options:\n"));
		for (; p->name != NULL; p++) {
			if (getsockopt(s, p->level, p->option,
						(void *)&value, &vlen) == -1) {
				DEBUGADD(5,("\tCould not test socket option %s.\n",
							p->name));
			} else {
				DEBUGADD(5,("\t%s = %d\n",
							p->name,value));
			}
		}
	}
 }

/****************************************************************************
 Set user socket options.
****************************************************************************/

void set_socket_options(int fd, const char *options)
{
	TALLOC_CTX *ctx = talloc_stackframe();
	char *tok;

	while (next_token_talloc(ctx, &options, &tok," \t,")) {
		int ret=0,i;
		int value = 1;
		char *p;
		bool got_value = false;

		if ((p = strchr_m(tok,'='))) {
			*p = 0;
			value = atoi(p+1);
			got_value = true;
		}

		for (i=0;socket_options[i].name;i++)
			if (strequal(socket_options[i].name,tok))
				break;

		if (!socket_options[i].name) {
			DEBUG(0,("Unknown socket option %s\n",tok));
			continue;
		}

		switch (socket_options[i].opttype) {
		case OPT_BOOL:
		case OPT_INT:
			ret = setsockopt(fd,socket_options[i].level,
					socket_options[i].option,
					(char *)&value,sizeof(int));
			break;

		case OPT_ON:
			if (got_value)
				DEBUG(0,("syntax error - %s "
					"does not take a value\n",tok));

			{
				int on = socket_options[i].value;
				ret = setsockopt(fd,socket_options[i].level,
					socket_options[i].option,
					(char *)&on,sizeof(int));
			}
			break;
		}

		if (ret != 0) {
			/* be aware that some systems like Solaris return
			 * EINVAL to a setsockopt() call when the client
			 * sent a RST previously - no need to worry */
			DEBUG(2,("Failed to set socket option %s (Error %s)\n",
				tok, strerror(errno) ));
		}
	}

	TALLOC_FREE(ctx);
	print_socket_options(fd);
}

/****************************************************************************
 Read from a socket.
****************************************************************************/

ssize_t read_udp_v4_socket(int fd,
			char *buf,
			size_t len,
			struct sockaddr_storage *psa)
{
	ssize_t ret;
	socklen_t socklen = sizeof(*psa);
	struct sockaddr_in *si = (struct sockaddr_in *)psa;

	memset((char *)psa,'\0',socklen);

	ret = (ssize_t)sys_recvfrom(fd,buf,len,0,
			(struct sockaddr *)psa,&socklen);
	if (ret <= 0) {
		/* Don't print a low debug error for a non-blocking socket. */
		if (errno == EAGAIN) {
			DEBUG(10,("read_udp_v4_socket: returned EAGAIN\n"));
		} else {
			DEBUG(2,("read_udp_v4_socket: failed. errno=%s\n",
				strerror(errno)));
		}
		return 0;
	}

	if (psa->ss_family != AF_INET) {
		DEBUG(2,("read_udp_v4_socket: invalid address family %d "
			"(not IPv4)\n", (int)psa->ss_family));
		return 0;
	}

	DEBUG(10,("read_udp_v4_socket: ip %s port %d read: %lu\n",
			inet_ntoa(si->sin_addr),
			si->sin_port,
			(unsigned long)ret));

	return ret;
}

/****************************************************************************
 Read data from a socket with a timout in msec.
 mincount = if timeout, minimum to read before returning
 maxcount = number to be read.
 time_out = timeout in milliseconds
****************************************************************************/

NTSTATUS read_socket_with_timeout(int fd, char *buf,
				  size_t mincnt, size_t maxcnt,
				  unsigned int time_out,
				  size_t *size_ret)
{
	fd_set fds;
	int selrtn;
	ssize_t readret;
	size_t nread = 0;
	struct timeval timeout;
	char addr[INET6_ADDRSTRLEN];

	/* just checking .... */
	if (maxcnt <= 0)
		return NT_STATUS_OK;

	/* Blocking read */
	if (time_out == 0) {
		if (mincnt == 0) {
			mincnt = maxcnt;
		}

		while (nread < mincnt) {
			readret = sys_recv(fd, buf + nread, maxcnt - nread, 0);

			if (readret == 0) {
				DEBUG(5,("read_socket_with_timeout: "
					"blocking read. EOF from client.\n"));
				return NT_STATUS_END_OF_FILE;
			}

			if (readret == -1) {
				if (fd == get_client_fd()) {
					/* Try and give an error message
					 * saying what client failed. */
					DEBUG(0,("read_socket_with_timeout: "
						"client %s read error = %s.\n",
						get_peer_addr(fd,addr,sizeof(addr)),
						strerror(errno) ));
				} else {
					DEBUG(0,("read_socket_with_timeout: "
						"read error = %s.\n",
						strerror(errno) ));
				}
				return map_nt_error_from_unix(errno);
			}
			nread += readret;
		}
		goto done;
	}

	/* Most difficult - timeout read */
	/* If this is ever called on a disk file and
	   mincnt is greater then the filesize then
	   system performance will suffer severely as
	   select always returns true on disk files */

	/* Set initial timeout */
	timeout.tv_sec = (time_t)(time_out / 1000);
	timeout.tv_usec = (long)(1000 * (time_out % 1000));

	for (nread=0; nread < mincnt; ) {
		FD_ZERO(&fds);
		FD_SET(fd,&fds);

		selrtn = sys_select_intr(fd+1,&fds,NULL,NULL,&timeout);

		/* Check if error */
		if (selrtn == -1) {
			/* something is wrong. Maybe the socket is dead? */
			if (fd == get_client_fd()) {
				/* Try and give an error message saying
				 * what client failed. */
				DEBUG(0,("read_socket_with_timeout: timeout "
				"read for client %s. select error = %s.\n",
				get_peer_addr(fd,addr,sizeof(addr)),
				strerror(errno) ));
			} else {
				DEBUG(0,("read_socket_with_timeout: timeout "
				"read. select error = %s.\n",
				strerror(errno) ));
			}
			return map_nt_error_from_unix(errno);
		}

		/* Did we timeout ? */
		if (selrtn == 0) {
			DEBUG(10,("read_socket_with_timeout: timeout read. "
				"select timed out.\n"));
			return NT_STATUS_IO_TIMEOUT;
		}

		readret = sys_recv(fd, buf+nread, maxcnt-nread, 0);

		if (readret == 0) {
			/* we got EOF on the file descriptor */
			DEBUG(5,("read_socket_with_timeout: timeout read. "
				"EOF from client.\n"));
			return NT_STATUS_END_OF_FILE;
		}

		if (readret == -1) {
			/* the descriptor is probably dead */
			if (fd == get_client_fd()) {
				/* Try and give an error message
				 * saying what client failed. */
				DEBUG(0,("read_socket_with_timeout: timeout "
					"read to client %s. read error = %s.\n",
					get_peer_addr(fd,addr,sizeof(addr)),
					strerror(errno) ));
			} else {
				DEBUG(0,("read_socket_with_timeout: timeout "
					"read. read error = %s.\n",
					strerror(errno) ));
			}
			return map_nt_error_from_unix(errno);
		}

		nread += readret;
	}

 done:
	/* Return the number we got */
	if (size_ret) {
		*size_ret = nread;
	}
	return NT_STATUS_OK;
}

/****************************************************************************
 Read data from the client, reading exactly N bytes.
****************************************************************************/

NTSTATUS read_data(int fd, char *buffer, size_t N)
{
	return read_socket_with_timeout(fd, buffer, N, N, 0, NULL);
}

/****************************************************************************
 Write all data from an iov array
****************************************************************************/

ssize_t write_data_iov(int fd, const struct iovec *orig_iov, int iovcnt)
{
	int i;
	size_t to_send;
	ssize_t thistime;
	size_t sent;
	struct iovec *iov_copy, *iov;

	to_send = 0;
	for (i=0; i<iovcnt; i++) {
		to_send += orig_iov[i].iov_len;
	}

	thistime = sys_writev(fd, orig_iov, iovcnt);
	if ((thistime <= 0) || (thistime == to_send)) {
		return thistime;
	}
	sent = thistime;

	/*
	 * We could not send everything in one call. Make a copy of iov that
	 * we can mess with. We keep a copy of the array start in iov_copy for
	 * the TALLOC_FREE, because we're going to modify iov later on,
	 * discarding elements.
	 */

	iov_copy = (struct iovec *)TALLOC_MEMDUP(
		talloc_tos(), orig_iov, sizeof(struct iovec) * iovcnt);

	if (iov_copy == NULL) {
		errno = ENOMEM;
		return -1;
	}
	iov = iov_copy;

	while (sent < to_send) {
		/*
		 * We have to discard "thistime" bytes from the beginning
		 * iov array, "thistime" contains the number of bytes sent
		 * via writev last.
		 */
		while (thistime > 0) {
			if (thistime < iov[0].iov_len) {
				char *new_base =
					(char *)iov[0].iov_base + thistime;
				iov[0].iov_base = new_base;
				iov[0].iov_len -= thistime;
				break;
			}
			thistime -= iov[0].iov_len;
			iov += 1;
			iovcnt -= 1;
		}

		thistime = sys_writev(fd, iov, iovcnt);
		if (thistime <= 0) {
			break;
		}
		sent += thistime;
	}

	TALLOC_FREE(iov_copy);
	return sent;
}

/****************************************************************************
 Write data to a fd.
****************************************************************************/

ssize_t write_data(int fd, const char *buffer, size_t N)
{
	ssize_t ret;
	struct iovec iov;

	iov.iov_base = CONST_DISCARD(char *, buffer);
	iov.iov_len = N;

	ret = write_data_iov(fd, &iov, 1);
	if (ret >= 0) {
		return ret;
	}

	if (fd == get_client_fd()) {
		char addr[INET6_ADDRSTRLEN];
		/*
		 * Try and give an error message saying what client failed.
		 */
		DEBUG(0, ("write_data: write failure in writing to client %s. "
			  "Error %s\n", get_peer_addr(fd,addr,sizeof(addr)),
			  strerror(errno)));
	} else {
		DEBUG(0,("write_data: write failure. Error = %s\n",
			 strerror(errno) ));
	}

	return -1;
}

/****************************************************************************
 Send a keepalive packet (rfc1002).
****************************************************************************/

bool send_keepalive(int client)
{
	unsigned char buf[4];

	buf[0] = SMBkeepalive;
	buf[1] = buf[2] = buf[3] = 0;

	return(write_data(client,(char *)buf,4) == 4);
}

/****************************************************************************
 Read 4 bytes of a smb packet and return the smb length of the packet.
 Store the result in the buffer.
 This version of the function will return a length of zero on receiving
 a keepalive packet.
 Timeout is in milliseconds.
****************************************************************************/

NTSTATUS read_smb_length_return_keepalive(int fd, char *inbuf,
					  unsigned int timeout,
					  size_t *len)
{
	int msg_type;
	NTSTATUS status;

	status = read_socket_with_timeout(fd, inbuf, 4, 4, timeout, NULL);

	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	*len = smb_len(inbuf);
	msg_type = CVAL(inbuf,0);

	if (msg_type == SMBkeepalive) {
		DEBUG(5,("Got keepalive packet\n"));
	}

	DEBUG(10,("got smb length of %lu\n",(unsigned long)(*len)));

	return NT_STATUS_OK;
}

/****************************************************************************
 Read 4 bytes of a smb packet and return the smb length of the packet.
 Store the result in the buffer. This version of the function will
 never return a session keepalive (length of zero).
 Timeout is in milliseconds.
****************************************************************************/

NTSTATUS read_smb_length(int fd, char *inbuf, unsigned int timeout,
			 size_t *len)
{
	uint8_t msgtype = SMBkeepalive;

	while (msgtype == SMBkeepalive) {
		NTSTATUS status;

		status = read_smb_length_return_keepalive(fd, inbuf, timeout,
							  len);
		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}

		msgtype = CVAL(inbuf, 0);
	}

	DEBUG(10,("read_smb_length: got smb length of %lu\n",
		  (unsigned long)len));

	return NT_STATUS_OK;
}

/****************************************************************************
 Read an smb from a fd.
 The timeout is in milliseconds.
 This function will return on receipt of a session keepalive packet.
 maxlen is the max number of bytes to return, not including the 4 byte
 length. If zero it means buflen limit.
 Doesn't check the MAC on signed packets.
****************************************************************************/

NTSTATUS receive_smb_raw(int fd, char *buffer, size_t buflen, unsigned int timeout,
			 size_t maxlen, size_t *p_len)
{
	size_t len;
	NTSTATUS status;

	status = read_smb_length_return_keepalive(fd,buffer,timeout,&len);

	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(10, ("receive_smb_raw: %s!\n", nt_errstr(status)));
		return status;
	}

	if (len > buflen) {
		DEBUG(0,("Invalid packet length! (%lu bytes).\n",
					(unsigned long)len));
		return NT_STATUS_INVALID_PARAMETER;
	}

	if(len > 0) {
		if (maxlen) {
			len = MIN(len,maxlen);
		}

		status = read_socket_with_timeout(
			fd, buffer+4, len, len, timeout, &len);

		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}

		/* not all of samba3 properly checks for packet-termination
		 * of strings. This ensures that we don't run off into
		 * empty space. */
		SSVAL(buffer+4,len, 0);
	}

	*p_len = len;
	return NT_STATUS_OK;
}

/****************************************************************************
 Open a socket of the specified type, port, and address for incoming data.
****************************************************************************/

int open_socket_in(int type,
		uint16_t port,
		int dlevel,
		const struct sockaddr_storage *psock,
		bool rebind)
{
	struct sockaddr_storage sock;
	int res;
	socklen_t slen = sizeof(struct sockaddr_in);

	sock = *psock;

#if defined(HAVE_IPV6)
	if (sock.ss_family == AF_INET6) {
		((struct sockaddr_in6 *)&sock)->sin6_port = htons(port);
		slen = sizeof(struct sockaddr_in6);
	}
#endif
	if (sock.ss_family == AF_INET) {
		((struct sockaddr_in *)&sock)->sin_port = htons(port);
	}

	res = socket(sock.ss_family, type, 0 );
	if( res == -1 ) {
		if( DEBUGLVL(0) ) {
			dbgtext( "open_socket_in(): socket() call failed: " );
			dbgtext( "%s\n", strerror( errno ) );
		}
		return -1;
	}

	/* This block sets/clears the SO_REUSEADDR and possibly SO_REUSEPORT. */
	{
		int val = rebind ? 1 : 0;
		if( setsockopt(res,SOL_SOCKET,SO_REUSEADDR,
					(char *)&val,sizeof(val)) == -1 ) {
			if( DEBUGLVL( dlevel ) ) {
				dbgtext( "open_socket_in(): setsockopt: " );
				dbgtext( "SO_REUSEADDR = %s ",
						val?"true":"false" );
				dbgtext( "on port %d failed ", port );
				dbgtext( "with error = %s\n", strerror(errno) );
			}
		}
#ifdef SO_REUSEPORT
		if( setsockopt(res,SOL_SOCKET,SO_REUSEPORT,
					(char *)&val,sizeof(val)) == -1 ) {
			if( DEBUGLVL( dlevel ) ) {
				dbgtext( "open_socket_in(): setsockopt: ");
				dbgtext( "SO_REUSEPORT = %s ",
						val?"true":"false");
				dbgtext( "on port %d failed ", port);
				dbgtext( "with error = %s\n", strerror(errno));
			}
		}
#endif /* SO_REUSEPORT */
	}

	/* now we've got a socket - we need to bind it */
	if (bind(res, (struct sockaddr *)&sock, slen) == -1 ) {
		if( DEBUGLVL(dlevel) && (port == SMB_PORT1 ||
				port == SMB_PORT2 || port == NMB_PORT) ) {
			char addr[INET6_ADDRSTRLEN];
			print_sockaddr(addr, sizeof(addr),
					&sock);
			dbgtext( "bind failed on port %d ", port);
			dbgtext( "socket_addr = %s.\n", addr);
			dbgtext( "Error = %s\n", strerror(errno));
		}
		close(res);
		return -1;
	}

	DEBUG( 10, ( "bind succeeded on port %d\n", port ) );
	return( res );
 }

struct open_socket_out_state {
	int fd;
	struct event_context *ev;
	struct sockaddr_storage ss;
	socklen_t salen;
	uint16_t port;
	int wait_nsec;
};

static void open_socket_out_connected(struct tevent_req *subreq);

static int open_socket_out_state_destructor(struct open_socket_out_state *s)
{
	if (s->fd != -1) {
		close(s->fd);
	}
	return 0;
}

/****************************************************************************
 Create an outgoing socket. timeout is in milliseconds.
**************************************************************************/

struct tevent_req *open_socket_out_send(TALLOC_CTX *mem_ctx,
					struct event_context *ev,
					const struct sockaddr_storage *pss,
					uint16_t port,
					int timeout)
{
	char addr[INET6_ADDRSTRLEN];
	struct tevent_req *result, *subreq;
	struct open_socket_out_state *state;
	NTSTATUS status;

	result = tevent_req_create(mem_ctx, &state,
				   struct open_socket_out_state);
	if (result == NULL) {
		return NULL;
	}
	state->ev = ev;
	state->ss = *pss;
	state->port = port;
	state->wait_nsec = 10000;
	state->salen = -1;

	state->fd = socket(state->ss.ss_family, SOCK_STREAM, 0);
	if (state->fd == -1) {
		status = map_nt_error_from_unix(errno);
		goto post_status;
	}
	talloc_set_destructor(state, open_socket_out_state_destructor);

	if (!tevent_req_set_endtime(
		    result, ev, timeval_current_ofs(0, timeout*1000))) {
		goto fail;
	}

#if defined(HAVE_IPV6)
	if (pss->ss_family == AF_INET6) {
		struct sockaddr_in6 *psa6;
		psa6 = (struct sockaddr_in6 *)&state->ss;
		psa6->sin6_port = htons(port);
		if (psa6->sin6_scope_id == 0
		    && IN6_IS_ADDR_LINKLOCAL(&psa6->sin6_addr)) {
			setup_linklocal_scope_id(
				(struct sockaddr *)&(state->ss));
		}
		state->salen = sizeof(struct sockaddr_in6);
	}
#endif
	if (pss->ss_family == AF_INET) {
		struct sockaddr_in *psa;
		psa = (struct sockaddr_in *)&state->ss;
		psa->sin_port = htons(port);
		state->salen = sizeof(struct sockaddr_in);
	}

	print_sockaddr(addr, sizeof(addr), &state->ss);
	DEBUG(3,("Connecting to %s at port %u\n", addr,	(unsigned int)port));

	subreq = async_connect_send(state, state->ev, state->fd,
				    (struct sockaddr *)&state->ss,
				    state->salen);
	if ((subreq == NULL)
	    || !tevent_req_set_endtime(
		    subreq, state->ev,
		    timeval_current_ofs(0, state->wait_nsec))) {
		goto fail;
	}
	tevent_req_set_callback(subreq, open_socket_out_connected, result);
	return result;

 post_status:
	tevent_req_nterror(result, status);
	return tevent_req_post(result, ev);
 fail:
	TALLOC_FREE(result);
	return NULL;
}

static void open_socket_out_connected(struct tevent_req *subreq)
{
	struct tevent_req *req =
		tevent_req_callback_data(subreq, struct tevent_req);
	struct open_socket_out_state *state =
		tevent_req_data(req, struct open_socket_out_state);
	int ret;
	int sys_errno;

	ret = async_connect_recv(subreq, &sys_errno);
	TALLOC_FREE(subreq);
	if (ret == 0) {
		tevent_req_done(req);
		return;
	}

	if (
#ifdef ETIMEDOUT
		(sys_errno == ETIMEDOUT) ||
#endif
		(sys_errno == EINPROGRESS) ||
		(sys_errno == EALREADY) ||
		(sys_errno == EAGAIN)) {

		/*
		 * retry
		 */

		if (state->wait_nsec < 250000) {
			state->wait_nsec *= 1.5;
		}

		subreq = async_connect_send(state, state->ev, state->fd,
					    (struct sockaddr *)&state->ss,
					    state->salen);
		if (tevent_req_nomem(subreq, req)) {
			return;
		}
		if (!tevent_req_set_endtime(
			    subreq, state->ev,
			    timeval_current_ofs(0, state->wait_nsec))) {
			tevent_req_nterror(req, NT_STATUS_NO_MEMORY);
			return;
		}
		tevent_req_set_callback(subreq, open_socket_out_connected, req);
		return;
	}

#ifdef EISCONN
	if (sys_errno == EISCONN) {
		tevent_req_done(req);
		return;
	}
#endif

	/* real error */
	tevent_req_nterror(req, map_nt_error_from_unix(sys_errno));
}

NTSTATUS open_socket_out_recv(struct tevent_req *req, int *pfd)
{
	struct open_socket_out_state *state =
		tevent_req_data(req, struct open_socket_out_state);
	NTSTATUS status;

	if (tevent_req_is_nterror(req, &status)) {
		return status;
	}
	*pfd = state->fd;
	state->fd = -1;
	return NT_STATUS_OK;
}

NTSTATUS open_socket_out(const struct sockaddr_storage *pss, uint16_t port,
			 int timeout, int *pfd)
{
	TALLOC_CTX *frame = talloc_stackframe();
	struct event_context *ev;
	struct tevent_req *req;
	NTSTATUS status = NT_STATUS_NO_MEMORY;

	ev = event_context_init(frame);
	if (ev == NULL) {
		goto fail;
	}

	req = open_socket_out_send(frame, ev, pss, port, timeout);
	if (req == NULL) {
		goto fail;
	}
	if (!tevent_req_poll(req, ev)) {
		status = NT_STATUS_INTERNAL_ERROR;
		goto fail;
	}
	status = open_socket_out_recv(req, pfd);
 fail:
	TALLOC_FREE(frame);
	return status;
}

struct open_socket_out_defer_state {
	struct event_context *ev;
	struct sockaddr_storage ss;
	uint16_t port;
	int timeout;
	int fd;
};

static void open_socket_out_defer_waited(struct tevent_req *subreq);
static void open_socket_out_defer_connected(struct tevent_req *subreq);

struct tevent_req *open_socket_out_defer_send(TALLOC_CTX *mem_ctx,
					      struct event_context *ev,
					      struct timeval wait_time,
					      const struct sockaddr_storage *pss,
					      uint16_t port,
					      int timeout)
{
	struct tevent_req *req, *subreq;
	struct open_socket_out_defer_state *state;

	req = tevent_req_create(mem_ctx, &state,
				struct open_socket_out_defer_state);
	if (req == NULL) {
		return NULL;
	}
	state->ev = ev;
	state->ss = *pss;
	state->port = port;
	state->timeout = timeout;

	subreq = tevent_wakeup_send(
		state, ev,
		timeval_current_ofs(wait_time.tv_sec, wait_time.tv_usec));
	if (subreq == NULL) {
		goto fail;
	}
	tevent_req_set_callback(subreq, open_socket_out_defer_waited, req);
	return req;
 fail:
	TALLOC_FREE(req);
	return NULL;
}

static void open_socket_out_defer_waited(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	struct open_socket_out_defer_state *state = tevent_req_data(
		req, struct open_socket_out_defer_state);
	bool ret;

	ret = tevent_wakeup_recv(subreq);
	TALLOC_FREE(subreq);
	if (!ret) {
		tevent_req_nterror(req, NT_STATUS_INTERNAL_ERROR);
		return;
	}

	subreq = open_socket_out_send(state, state->ev, &state->ss,
				      state->port, state->timeout);
	if (tevent_req_nomem(subreq, req)) {
		return;
	}
	tevent_req_set_callback(subreq, open_socket_out_defer_connected, req);
}

static void open_socket_out_defer_connected(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	struct open_socket_out_defer_state *state = tevent_req_data(
		req, struct open_socket_out_defer_state);
	NTSTATUS status;

	status = open_socket_out_recv(subreq, &state->fd);
	TALLOC_FREE(subreq);
	if (!NT_STATUS_IS_OK(status)) {
		tevent_req_nterror(req, status);
		return;
	}
	tevent_req_done(req);
}

NTSTATUS open_socket_out_defer_recv(struct tevent_req *req, int *pfd)
{
	struct open_socket_out_defer_state *state = tevent_req_data(
		req, struct open_socket_out_defer_state);
	NTSTATUS status;

	if (tevent_req_is_nterror(req, &status)) {
		return status;
	}
	*pfd = state->fd;
	state->fd = -1;
	return NT_STATUS_OK;
}

/*******************************************************************
 Create an outgoing TCP socket to the first addr that connects.

 This is for simultaneous connection attempts to port 445 and 139 of a host
 or for simultatneous connection attempts to multiple DCs at once.  We return
 a socket fd of the first successful connection.

 @param[in] addrs list of Internet addresses and ports to connect to
 @param[in] num_addrs number of address/port pairs in the addrs list
 @param[in] timeout time after which we stop waiting for a socket connection
            to succeed, given in milliseconds
 @param[out] fd_index the entry in addrs which we successfully connected to
 @param[out] fd fd of the open and connected socket
 @return true on a successful connection, false if all connection attempts
         failed or we timed out
*******************************************************************/

bool open_any_socket_out(struct sockaddr_storage *addrs, int num_addrs,
			 int timeout, int *fd_index, int *fd)
{
	int i, resulting_index, res;
	int *sockets;
	bool good_connect;

	fd_set r_fds, wr_fds;
	struct timeval tv;
	int maxfd;

	int connect_loop = 10000; /* 10 milliseconds */

	timeout *= 1000; 	/* convert to microseconds */

	sockets = SMB_MALLOC_ARRAY(int, num_addrs);

	if (sockets == NULL)
		return false;

	resulting_index = -1;

	for (i=0; i<num_addrs; i++)
		sockets[i] = -1;

	for (i=0; i<num_addrs; i++) {
		sockets[i] = socket(addrs[i].ss_family, SOCK_STREAM, 0);
		if (sockets[i] < 0)
			goto done;
		set_blocking(sockets[i], false);
	}

 connect_again:
	good_connect = false;

	for (i=0; i<num_addrs; i++) {
		const struct sockaddr * a = 
		    (const struct sockaddr *)&(addrs[i]);

		if (sockets[i] == -1)
			continue;

		if (sys_connect(sockets[i], a) == 0) {
			/* Rather unlikely as we are non-blocking, but it
			 * might actually happen. */
			resulting_index = i;
			goto done;
		}

		if (errno == EINPROGRESS || errno == EALREADY ||
#ifdef EISCONN
			errno == EISCONN ||
#endif
		    errno == EAGAIN || errno == EINTR) {
			/* These are the error messages that something is
			   progressing. */
			good_connect = true;
		} else if (errno != 0) {
			/* There was a direct error */
			close(sockets[i]);
			sockets[i] = -1;
		}
	}

	if (!good_connect) {
		/* All of the connect's resulted in real error conditions */
		goto done;
	}

	/* Lets see if any of the connect attempts succeeded */

	maxfd = 0;
	FD_ZERO(&wr_fds);
	FD_ZERO(&r_fds);

	for (i=0; i<num_addrs; i++) {
		if (sockets[i] == -1)
			continue;
		FD_SET(sockets[i], &wr_fds);
		FD_SET(sockets[i], &r_fds);
		if (sockets[i]>maxfd)
			maxfd = sockets[i];
	}

	tv.tv_sec = 0;
	tv.tv_usec = connect_loop;

	res = sys_select_intr(maxfd+1, &r_fds, &wr_fds, NULL, &tv);

	if (res < 0)
		goto done;

	if (res == 0)
		goto next_round;

	for (i=0; i<num_addrs; i++) {

		if (sockets[i] == -1)
			continue;

		/* Stevens, Network Programming says that if there's a
		 * successful connect, the socket is only writable. Upon an
		 * error, it's both readable and writable. */

		if (FD_ISSET(sockets[i], &r_fds) &&
		    FD_ISSET(sockets[i], &wr_fds)) {
			/* readable and writable, so it's an error */
			close(sockets[i]);
			sockets[i] = -1;
			continue;
		}

		if (!FD_ISSET(sockets[i], &r_fds) &&
		    FD_ISSET(sockets[i], &wr_fds)) {
			/* Only writable, so it's connected */
			resulting_index = i;
			goto done;
		}
	}

 next_round:

	timeout -= connect_loop;
	if (timeout <= 0)
		goto done;
	connect_loop *= 1.5;
	if (connect_loop > timeout)
		connect_loop = timeout;
	goto connect_again;

 done:
	for (i=0; i<num_addrs; i++) {
		if (i == resulting_index)
			continue;
		if (sockets[i] >= 0)
			close(sockets[i]);
	}

	if (resulting_index >= 0) {
		*fd_index = resulting_index;
		*fd = sockets[*fd_index];
		set_blocking(*fd, true);
	}

	free(sockets);

	return (resulting_index >= 0);
}
/****************************************************************************
 Open a connected UDP socket to host on port
**************************************************************************/

int open_udp_socket(const char *host, int port)
{
	int type = SOCK_DGRAM;
	struct sockaddr_in sock_out;
	int res;
	struct in_addr addr;

	addr = interpret_addr2(host);

	res = socket(PF_INET, type, 0);
	if (res == -1) {
		return -1;
	}

	memset((char *)&sock_out,'\0',sizeof(sock_out));
	putip((char *)&sock_out.sin_addr,(char *)&addr);
	sock_out.sin_port = htons(port);
	sock_out.sin_family = PF_INET;

	if (sys_connect(res,(struct sockaddr *)&sock_out)) {
		close(res);
		return -1;
	}

	return res;
}

/*******************************************************************
 Return the IP addr of the remote end of a socket as a string.
 Optionally return the struct sockaddr_storage.
 ******************************************************************/

static const char *get_peer_addr_internal(int fd,
				char *addr_buf,
				size_t addr_buf_len,
				struct sockaddr *pss,
				socklen_t *plength)
{
	struct sockaddr_storage ss;
	socklen_t length = sizeof(ss);

	strlcpy(addr_buf,"0.0.0.0",addr_buf_len);

	if (fd == -1) {
		return addr_buf;
	}

	if (pss == NULL) {
		pss = (struct sockaddr *)&ss;
		plength = &length;
	}

	if (getpeername(fd, (struct sockaddr *)pss, plength) < 0) {
		DEBUG(0,("getpeername failed. Error was %s\n",
					strerror(errno) ));
		return addr_buf;
	}

	print_sockaddr_len(addr_buf,
			addr_buf_len,
			pss,
			*plength);
	return addr_buf;
}

/*******************************************************************
 Matchname - determine if host name matches IP address. Used to
 confirm a hostname lookup to prevent spoof attacks.
******************************************************************/

static bool matchname(const char *remotehost,
		const struct sockaddr *pss,
		socklen_t len)
{
	struct addrinfo *res = NULL;
	struct addrinfo *ailist = NULL;
	char addr_buf[INET6_ADDRSTRLEN];
	bool ret = interpret_string_addr_internal(&ailist,
			remotehost,
			AI_ADDRCONFIG|AI_CANONNAME);

	if (!ret || ailist == NULL) {
		DEBUG(3,("matchname: getaddrinfo failed for "
			"name %s [%s]\n",
			remotehost,
			gai_strerror(ret) ));
		return false;
	}

	/*
	 * Make sure that getaddrinfo() returns the "correct" host name.
	 */

	if (ailist->ai_canonname == NULL ||
		(!strequal(remotehost, ailist->ai_canonname) &&
		 !strequal(remotehost, "localhost"))) {
		DEBUG(0,("matchname: host name/name mismatch: %s != %s\n",
			 remotehost,
			 ailist->ai_canonname ?
				 ailist->ai_canonname : "(NULL)"));
		freeaddrinfo(ailist);
		return false;
	}

	/* Look up the host address in the address list we just got. */
	for (res = ailist; res; res = res->ai_next) {
		if (!res->ai_addr) {
			continue;
		}
		if (sockaddr_equal((const struct sockaddr *)res->ai_addr,
					(struct sockaddr *)pss)) {
			freeaddrinfo(ailist);
			return true;
		}
	}

	/*
	 * The host name does not map to the original host address. Perhaps
	 * someone has compromised a name server. More likely someone botched
	 * it, but that could be dangerous, too.
	 */

	DEBUG(0,("matchname: host name/address mismatch: %s != %s\n",
		print_sockaddr_len(addr_buf,
			sizeof(addr_buf),
			pss,
			len),
		 ailist->ai_canonname ? ailist->ai_canonname : "(NULL)"));

	if (ailist) {
		freeaddrinfo(ailist);
	}
	return false;
}

/*******************************************************************
 Deal with the singleton cache.
******************************************************************/

struct name_addr_pair {
	struct sockaddr_storage ss;
	const char *name;
};

/*******************************************************************
 Lookup a name/addr pair. Returns memory allocated from memcache.
******************************************************************/

static bool lookup_nc(struct name_addr_pair *nc)
{
	DATA_BLOB tmp;

	ZERO_STRUCTP(nc);

	if (!memcache_lookup(
			NULL, SINGLETON_CACHE,
			data_blob_string_const_null("get_peer_name"),
			&tmp)) {
		return false;
	}

	memcpy(&nc->ss, tmp.data, sizeof(nc->ss));
	nc->name = (const char *)tmp.data + sizeof(nc->ss);
	return true;
}

/*******************************************************************
 Save a name/addr pair.
******************************************************************/

static void store_nc(const struct name_addr_pair *nc)
{
	DATA_BLOB tmp;
	size_t namelen = strlen(nc->name);

	tmp = data_blob(NULL, sizeof(nc->ss) + namelen + 1);
	if (!tmp.data) {
		return;
	}
	memcpy(tmp.data, &nc->ss, sizeof(nc->ss));
	memcpy(tmp.data+sizeof(nc->ss), nc->name, namelen+1);

	memcache_add(NULL, SINGLETON_CACHE,
			data_blob_string_const_null("get_peer_name"),
			tmp);
	data_blob_free(&tmp);
}

/*******************************************************************
 Return the DNS name of the remote end of a socket.
******************************************************************/

const char *get_peer_name(int fd, bool force_lookup)
{
	struct name_addr_pair nc;
	char addr_buf[INET6_ADDRSTRLEN];
	struct sockaddr_storage ss;
	socklen_t length = sizeof(ss);
	const char *p;
	int ret;
	char name_buf[MAX_DNS_NAME_LENGTH];
	char tmp_name[MAX_DNS_NAME_LENGTH];

	/* reverse lookups can be *very* expensive, and in many
	   situations won't work because many networks don't link dhcp
	   with dns. To avoid the delay we avoid the lookup if
	   possible */
	if (!lp_hostname_lookups() && (force_lookup == false)) {
		length = sizeof(nc.ss);
		nc.name = get_peer_addr_internal(fd, addr_buf, sizeof(addr_buf),
			(struct sockaddr *)&nc.ss, &length);
		store_nc(&nc);
		lookup_nc(&nc);
		return nc.name ? nc.name : "UNKNOWN";
	}

	lookup_nc(&nc);

	memset(&ss, '\0', sizeof(ss));
	p = get_peer_addr_internal(fd, addr_buf, sizeof(addr_buf), (struct sockaddr *)&ss, &length);

	/* it might be the same as the last one - save some DNS work */
	if (sockaddr_equal((struct sockaddr *)&ss, (struct sockaddr *)&nc.ss)) {
		return nc.name ? nc.name : "UNKNOWN";
	}

	/* Not the same. We need to lookup. */
	if (fd == -1) {
		return "UNKNOWN";
	}

	/* Look up the remote host name. */
	ret = sys_getnameinfo((struct sockaddr *)&ss,
			length,
			name_buf,
			sizeof(name_buf),
			NULL,
			0,
			0);

	if (ret) {
		DEBUG(1,("get_peer_name: getnameinfo failed "
			"for %s with error %s\n",
			p,
			gai_strerror(ret)));
		strlcpy(name_buf, p, sizeof(name_buf));
	} else {
		if (!matchname(name_buf, (struct sockaddr *)&ss, length)) {
			DEBUG(0,("Matchname failed on %s %s\n",name_buf,p));
			strlcpy(name_buf,"UNKNOWN",sizeof(name_buf));
		}
	}

	/* can't pass the same source and dest strings in when you
	   use --enable-developer or the clobber_region() call will
	   get you */

	strlcpy(tmp_name, name_buf, sizeof(tmp_name));
	alpha_strcpy(name_buf, tmp_name, "_-.", sizeof(name_buf));
	if (strstr(name_buf,"..")) {
		strlcpy(name_buf, "UNKNOWN", sizeof(name_buf));
	}

	nc.name = name_buf;
	nc.ss = ss;

	store_nc(&nc);
	lookup_nc(&nc);
	return nc.name ? nc.name : "UNKNOWN";
}

/*******************************************************************
 Return the IP addr of the remote end of a socket as a string.
 ******************************************************************/

const char *get_peer_addr(int fd, char *addr, size_t addr_len)
{
	return get_peer_addr_internal(fd, addr, addr_len, NULL, NULL);
}

/*******************************************************************
 Create protected unix domain socket.

 Some unixes cannot set permissions on a ux-dom-sock, so we
 have to make sure that the directory contains the protection
 permissions instead.
 ******************************************************************/

int create_pipe_sock(const char *socket_dir,
		     const char *socket_name,
		     mode_t dir_perms)
{
#ifdef HAVE_UNIXSOCKET
	struct sockaddr_un sunaddr;
	struct stat st;
	int sock;
	mode_t old_umask;
	char *path = NULL;

	old_umask = umask(0);

	/* Create the socket directory or reuse the existing one */

	if (lstat(socket_dir, &st) == -1) {
		if (errno == ENOENT) {
			/* Create directory */
			if (mkdir(socket_dir, dir_perms) == -1) {
				DEBUG(0, ("error creating socket directory "
					"%s: %s\n", socket_dir,
					strerror(errno)));
				goto out_umask;
			}
		} else {
			DEBUG(0, ("lstat failed on socket directory %s: %s\n",
				socket_dir, strerror(errno)));
			goto out_umask;
		}
	} else {
		/* Check ownership and permission on existing directory */
		if (!S_ISDIR(st.st_mode)) {
			DEBUG(0, ("socket directory %s isn't a directory\n",
				socket_dir));
			goto out_umask;
		}
		if ((st.st_uid != sec_initial_uid()) ||
				((st.st_mode & 0777) != dir_perms)) {
			DEBUG(0, ("invalid permissions on socket directory "
				"%s\n", socket_dir));
			goto out_umask;
		}
	}

	/* Create the socket file */

	sock = socket(AF_UNIX, SOCK_STREAM, 0);

	if (sock == -1) {
		DEBUG(0, ("create_pipe_sock: socket error %s\n",
			strerror(errno) ));
                goto out_close;
	}

	if (asprintf(&path, "%s/%s", socket_dir, socket_name) == -1) {
                goto out_close;
	}

	unlink(path);
	memset(&sunaddr, 0, sizeof(sunaddr));
	sunaddr.sun_family = AF_UNIX;
	strlcpy(sunaddr.sun_path, path, sizeof(sunaddr.sun_path));

	if (bind(sock, (struct sockaddr *)&sunaddr, sizeof(sunaddr)) == -1) {
		DEBUG(0, ("bind failed on pipe socket %s: %s\n", path,
			strerror(errno)));
		goto out_close;
	}

	if (listen(sock, 5) == -1) {
		DEBUG(0, ("listen failed on pipe socket %s: %s\n", path,
			strerror(errno)));
		goto out_close;
	}

	SAFE_FREE(path);

	umask(old_umask);
	return sock;

out_close:
	SAFE_FREE(path);
	if (sock != -1)
		close(sock);

out_umask:
	umask(old_umask);
	return -1;

#else
        DEBUG(0, ("create_pipe_sock: No Unix sockets on this system\n"));
        return -1;
#endif /* HAVE_UNIXSOCKET */
}

/****************************************************************************
 Get my own canonical name, including domain.
****************************************************************************/

const char *get_mydnsfullname(void)
{
	struct addrinfo *res = NULL;
	char my_hostname[HOST_NAME_MAX];
	bool ret;
	DATA_BLOB tmp;

	if (memcache_lookup(NULL, SINGLETON_CACHE,
			data_blob_string_const_null("get_mydnsfullname"),
			&tmp)) {
		SMB_ASSERT(tmp.length > 0);
		return (const char *)tmp.data;
	}

	/* get my host name */
	if (gethostname(my_hostname, sizeof(my_hostname)) == -1) {
		DEBUG(0,("get_mydnsfullname: gethostname failed\n"));
		return NULL;
	}

	/* Ensure null termination. */
	my_hostname[sizeof(my_hostname)-1] = '\0';

	ret = interpret_string_addr_internal(&res,
				my_hostname,
				AI_ADDRCONFIG|AI_CANONNAME);

	if (!ret || res == NULL) {
		DEBUG(3,("get_mydnsfullname: getaddrinfo failed for "
			"name %s [%s]\n",
			my_hostname,
			gai_strerror(ret) ));
		return NULL;
	}

	/*
	 * Make sure that getaddrinfo() returns the "correct" host name.
	 */

	if (res->ai_canonname == NULL) {
		DEBUG(3,("get_mydnsfullname: failed to get "
			"canonical name for %s\n",
			my_hostname));
		freeaddrinfo(res);
		return NULL;
	}

	/* This copies the data, so we must do a lookup
	 * afterwards to find the value to return.
	 */

	memcache_add(NULL, SINGLETON_CACHE,
			data_blob_string_const_null("get_mydnsfullname"),
			data_blob_string_const_null(res->ai_canonname));

	if (!memcache_lookup(NULL, SINGLETON_CACHE,
			data_blob_string_const_null("get_mydnsfullname"),
			&tmp)) {
		tmp = data_blob_talloc(talloc_tos(), res->ai_canonname,
				strlen(res->ai_canonname) + 1);
	}

	freeaddrinfo(res);

	return (const char *)tmp.data;
}

/************************************************************
 Is this my name ?
************************************************************/

bool is_myname_or_ipaddr(const char *s)
{
	TALLOC_CTX *ctx = talloc_tos();
	char addr[INET6_ADDRSTRLEN];
	char *name = NULL;
	const char *dnsname;
	char *servername = NULL;

	if (!s) {
		return false;
	}

	/* Santize the string from '\\name' */
	name = talloc_strdup(ctx, s);
	if (!name) {
		return false;
	}

	servername = strrchr_m(name, '\\' );
	if (!servername) {
		servername = name;
	} else {
		servername++;
	}

	/* Optimize for the common case */
	if (strequal(servername, global_myname())) {
		return true;
	}

	/* Check for an alias */
	if (is_myname(servername)) {
		return true;
	}

	/* Check for loopback */
	if (strequal(servername, "127.0.0.1") ||
			strequal(servername, "::1")) {
		return true;
	}

	if (strequal(servername, "localhost")) {
		return true;
	}

	/* Maybe it's my dns name */
	dnsname = get_mydnsfullname();
	if (dnsname && strequal(servername, dnsname)) {
		return true;
	}

	/* Handle possible CNAME records - convert to an IP addr. */
	if (!is_ipaddress(servername)) {
		/* Use DNS to resolve the name, but only the first address */
		struct sockaddr_storage ss;
		if (interpret_string_addr(&ss, servername, 0)) {
			print_sockaddr(addr,
					sizeof(addr),
					&ss);
			servername = addr;
		}
	}

	/* Maybe its an IP address? */
	if (is_ipaddress(servername)) {
		struct sockaddr_storage ss;
		struct iface_struct *nics;
		int i, n;

		if (!interpret_string_addr(&ss, servername, AI_NUMERICHOST)) {
			return false;
		}

		if (ismyaddr((struct sockaddr *)&ss)) {
			return true;
		}

		if (is_zero_addr((struct sockaddr *)&ss) || 
			is_loopback_addr((struct sockaddr *)&ss)) {
			return false;
		}

		nics = TALLOC_ARRAY(ctx, struct iface_struct,
					MAX_INTERFACES);
		if (!nics) {
			return false;
		}
		n = get_interfaces(nics, MAX_INTERFACES);
		for (i=0; i<n; i++) {
			if (sockaddr_equal((struct sockaddr *)&nics[i].ip, (struct sockaddr *)&ss)) {
				TALLOC_FREE(nics);
				return true;
			}
		}
		TALLOC_FREE(nics);
	}

	/* No match */
	return false;
}

/*
 * Read an smb packet asynchronously, discard keepalives
 */

struct read_smb_state {
	struct tevent_context *ev;
	int fd;
	uint8_t *buf;
};

static ssize_t read_smb_more(uint8_t *buf, size_t buflen, void *private_data);
static void read_smb_done(struct tevent_req *subreq);

struct tevent_req *read_smb_send(TALLOC_CTX *mem_ctx,
				 struct tevent_context *ev,
				 int fd)
{
	struct tevent_req *result, *subreq;
	struct read_smb_state *state;

	result = tevent_req_create(mem_ctx, &state, struct read_smb_state);
	if (result == NULL) {
		return NULL;
	}
	state->ev = ev;
	state->fd = fd;

	subreq = read_packet_send(state, ev, fd, 4, read_smb_more, NULL);
	if (subreq == NULL) {
		goto fail;
	}
	tevent_req_set_callback(subreq, read_smb_done, result);
	return result;
 fail:
	TALLOC_FREE(result);
	return NULL;
}

static ssize_t read_smb_more(uint8_t *buf, size_t buflen, void *private_data)
{
	if (buflen > 4) {
		return 0;	/* We've been here, we're done */
	}
	return smb_len_large(buf);
}

static void read_smb_done(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	struct read_smb_state *state = tevent_req_data(
		req, struct read_smb_state);
	ssize_t len;
	int err;

	len = read_packet_recv(subreq, state, &state->buf, &err);
	TALLOC_FREE(subreq);
	if (len == -1) {
		tevent_req_error(req, err);
		return;
	}

	if (CVAL(state->buf, 0) == SMBkeepalive) {
		subreq = read_packet_send(state, state->ev, state->fd, 4,
					  read_smb_more, NULL);
		if (tevent_req_nomem(subreq, req)) {
			return;
		}
		tevent_req_set_callback(subreq, read_smb_done, req);
		return;
	}
	tevent_req_done(req);
}

ssize_t read_smb_recv(struct tevent_req *req, TALLOC_CTX *mem_ctx,
		      uint8_t **pbuf, int *perrno)
{
	struct read_smb_state *state = tevent_req_data(
		req, struct read_smb_state);

	if (tevent_req_is_unix_error(req, perrno)) {
		return -1;
	}
	*pbuf = talloc_move(mem_ctx, &state->buf);
	return talloc_get_size(*pbuf);
}

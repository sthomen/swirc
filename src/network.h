#ifndef NETWORK_H
#define NETWORK_H

#if defined(UNIX)
#include "net-unix.h"
#elif defined(WIN32)
#include "net-w32.h"
#endif

#include "x509_check_host.h"

struct network_connect_context {
    char *server;
    char *port;
    char *password;
    char *username;
    char *rl_name;
    char *nickname;
};

typedef PTR_ARGS_NONNULL int (*NET_SEND_FN)(const char *, ...);
typedef PTR_ARGS_NONNULL int (*NET_RECV_FN)(struct network_recv_context *, char *, int);

extern NET_SEND_FN net_send;
extern NET_RECV_FN net_recv;

extern volatile bool g_connection_in_progress;
extern volatile bool g_on_air;

/*lint -sem(net_addr_resolve, r_null) */

bool		 is_sasl_enabled(void);
struct addrinfo *net_addr_resolve(const char *host, const char *port);
void		 net_connect(const struct network_connect_context *);
void		 net_irc_listen(void);

void	net_ssl_init(void);
void	net_ssl_deinit(void);
void	net_ssl_close(void);
int	net_ssl_start(void);
int	net_ssl_send(const char *fmt, ...);
int	net_ssl_recv(struct network_recv_context *, char *, int);
int	net_ssl_check_hostname(const char *, unsigned int);

#endif

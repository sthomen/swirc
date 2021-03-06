#ifndef CMD_CONNECT_H
#define CMD_CONNECT_H

void do_connect     (const char *server, const char *port);
void set_ssl_on     (void);
void set_ssl_off    (void);
bool is_ssl_enabled (void);

void cmd_connect    (const char *);
void cmd_disconnect (const char *);

#endif

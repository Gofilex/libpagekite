/******************************************************************************
pkmanager.h - A manager for multiple pagekite connections.

This file is Copyright 2011, 2012, The Beanstalks Project ehf.

This program is free software: you can redistribute it and/or modify it under
the terms of the  GNU  Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,  but  WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for more
details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see: <http://www.gnu.org/licenses/>

Note: For alternate license terms, see the file COPYING.md.

******************************************************************************/

#define PK_HOUSEKEEPING_INTERVAL_MIN   2.0  /* Seconds */
#ifndef ANDROID
#define PK_HOUSEKEEPING_INTERVAL_MAX  60.0  /* 1 minute */
#else
#define PK_HOUSEKEEPING_INTERVAL_MAX 240.0  /* 4 minutes on mobile */
#endif
#define PK_CHECK_WORLD_INTERVAL       3600  /* 1 hour */

struct pk_frontend;
struct pk_backend_conn;
struct pk_manager;
struct pk_job;
struct pk_job_pile;

/* These are also written to the conn.status field, using the fourth byte. */
#define FE_STATUS_AUTO      0x00000000  /* For use in pkm_add_frontend     */
#define FE_STATUS_WANTED    0x01000000  /* Algorithm chose this FE         */
#define FE_STATUS_NAILED_UP 0x02000000  /* User chose this FE              */
#define FE_STATUS_IN_DNS    0x04000000  /* This FE is in DNS               */
#define FE_STATUS_REJECTED  0x08000000  /* Front-end rejected connection   */
#define FE_STATUS_LAME      0x10000000  /* Front-end is going offline      */
#define FE_STATUS_IS_FAST   0x20000000  /* This is a fast front-end        */
#define FE_STATUS_BITS      0xff000000
struct pk_frontend {
  char*                   fe_hostname;
  int                     fe_port;
  char                    fe_session[PK_HANDSHAKE_SESSIONID_MAX];
  struct addrinfo*        ai;
  int                     priority;
  struct pk_conn          conn;
  struct pk_parser*       parser;
  struct pk_manager*      manager;
  int                     request_count;
  struct pk_kite_request* requests;
};

/* These are also written to the conn.status field, using the third byte. */
#define BE_STATUS_EOF_READ       0x00010000
#define BE_STATUS_EOF_WRITE      0x00020000
#define BE_STATUS_EOF_THROTTLED  0x00040000
#define BE_MAX_SID_SIZE          8
struct pk_backend_conn {
  char                sid[BE_MAX_SID_SIZE];
  struct pk_frontend* frontend;
  struct pk_pagekite* kite;
  struct pk_conn      conn;
};

#define MIN_KITE_ALLOC   4
#define MIN_FE_ALLOC     2
#define MIN_CONN_ALLOC  16
#define PK_MANAGER_BUFSIZE(k, f, c, ps) \
                           (1 + sizeof(struct pk_manager) \
                            + sizeof(struct pk_pagekite) * k \
                            + sizeof(struct pk_frontend) * f \
                            + sizeof(struct pk_kite_request) * f * k \
                            + ps * f \
                            + sizeof(struct pk_backend_conn) * c \
                            + sizeof(struct pk_job) * (c+f))
#define PK_MANAGER_MINSIZE PK_MANAGER_BUFSIZE(MIN_KITE_ALLOC, MIN_FE_ALLOC, \
                                              MIN_CONN_ALLOC, PARSER_BYTES_MIN)

struct pk_manager {
  int                      buffer_bytes_free;
  char*                    buffer;
  char*                    buffer_base;
  int                      kite_max;
  struct pk_pagekite*      kites;
  int                      frontend_max;
  struct pk_frontend*      frontends;
  int                      be_conn_max;
  struct pk_backend_conn*  be_conns;

  pthread_t                main_thread;
  pthread_mutex_t          loop_lock;
  struct ev_loop*          loop;
  ev_async                 interrupt;
  ev_async                 quit;
  ev_timer                 timer;
  int                      want_spare_frontends;
  time_t                   last_world_update;

  char*                    dynamic_dns_url;

  pthread_t                blocking_thread;
  struct pk_job_pile       blocking_jobs;
};


void* pkm_run                       (void *);
int pkm_run_in_thread               (struct pk_manager*);
int pkm_wait_thread                 (struct pk_manager*);
int pkm_stop_thread                 (struct pk_manager*);
int pkm_reconnect_all               (struct pk_manager*);

struct pk_manager*   pkm_manager_init(struct ev_loop*,
                                      int, char*, int, int, int, char*);
void                 pkm_reset_timer(struct pk_manager*);
struct pk_pagekite*  pkm_add_kite(struct pk_manager*,
                                  const char*, const char*, int, const char*,
                                  const char*, int);
struct pk_pagekite*  pkm_find_kite(struct pk_manager*,
                                   const char*, const char*, int);
ssize_t              pkm_write_chunked(struct pk_frontend*,
                                       struct pk_backend_conn*,
                                       ssize_t, char*);
int                  pkm_add_frontend(struct pk_manager*,
                                      const char*, int, int);
struct pk_frontend*  pkm_add_frontend_ai(struct pk_manager*, struct addrinfo*,
                                         const char*, int, int);
void                 pkm_reset_conn(struct pk_conn*);

ssize_t              pkm_write_data(struct pk_conn*, ssize_t, char*);
ssize_t              pkm_read_data(struct pk_conn*);
ssize_t              pkm_flush(struct pk_conn*, char*, ssize_t, int, char*);
void                 pkm_parse_eof(struct pk_backend_conn*, char*);
int                  pkm_update_io(struct pk_frontend*, struct pk_backend_conn*);
void                 pkm_flow_control_fe(struct pk_frontend*, flow_op);
void                 pkm_flow_control_conn(struct pk_conn*, flow_op);

/* Backend connection handling */
struct pk_backend_conn*  pkm_connect_be(struct pk_frontend*, struct pk_chunk*);
struct pk_backend_conn*  pkm_alloc_be_conn(struct pk_manager*,
                                           struct pk_frontend*, char*);
struct pk_backend_conn*  pkm_find_be_conn(struct pk_manager*,
                                          struct pk_frontend*,  char*);
void                     pkm_free_be_conn(struct pk_backend_conn*);

void pkm_chunk_cb(struct pk_frontend*, struct pk_chunk *);
void pkm_tunnel_readable_cb(EV_P_ ev_io *, int);
void pkm_tunnel_writable_cb(EV_P_ ev_io *, int);
void pkm_be_conn_readable_cb(EV_P_ ev_io *, int);
void pkm_be_conn_writable_cb(EV_P_ ev_io *, int);
void pkm_timer_cb(EV_P_ ev_timer *w, int);


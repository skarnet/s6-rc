/* ISC license. */

#include <sys/types.h>
#include <stdint.h>

#include <skalibs/posixplz.h>
#include <skalibs/bytestr.h>
#include <skalibs/types.h>
#include <skalibs/env.h>
#include <skalibs/strerr2.h>
#include <skalibs/djbunix.h>
#include <skalibs/cdb.h>

#include <s6/accessrules.h>

#include "clientrules.h"

static unsigned int rulestype = 0 ;
static char const *rules = 0 ;
static int cdbfd = -1 ;
static struct cdb cdbmap = CDB_ZERO ;

void clientrules_init (unsigned int type, char const *s)
{
  rulestype = type ;
  rules = s ;
  if (rulestype == 2)
  {
    cdbfd = open_readb(rules) ;
    if (cdbfd < 0) strerr_diefu3sys(111, "open ", rules, " for reading") ;
    if (cdb_init(&cdbmap, cdbfd) < 0)
      strerr_diefu2sys(111, "cdb_init ", rules) ;
  }
}

void clientrules_reload ()
{
  int fd ;
  struct cdb c = CDB_ZERO ;
  if (rulestype != 2) break ;
  fd = open_readb(rules) ;
  if (fd < 0) break ;
  if (cdb_init(&c, fd) < 0)
  {
    fd_close(fd) ;
    break ;
  }
  cdb_free(&cdbmap) ;
  fd_close(cdbfd) ;
  cdbfd = fd ;
  cdbmap = c ;
}

static inline uint8_t parse_env (char const *const *envp)
{
  uint8_t perms = 0 ;
  for (; *envp ; envp++)
  {
    if (str_start(*envp, "query=")) perms |= 1 ;
    if (str_start(*envp, "monitor=")) perms |= 2 ;
    if (str_start(*envp, "change=")) perms |= 4 ;
    if (str_start(*envp, "event=")) perms |= 8 ;
    if (str_start(*envp, "admin=")) perms |= 16 ;
  }
  return perms ;
}

int clientrules_check (int fd, uint8_t *perms)
{
  s6_accessrules_params_t params = S6_ACCESSRULES_PARAMS_ZERO ;
  s6_accessrules_result_t result = S6_ACCESSRULES_ERROR ;
  uid_t uid ;
  gid_t gid ;

  if (getpeereid(fd, &uid, &gid) < 0)
  {
    if (verbosity) strerr_warnwu1sys("getpeereid") ;
    return 0 ;
  }

  switch (rulestype)
  {
    case 1 :
      result = s6_accessrules_uidgid_fs(uid, gid, rules, &params) ; break ;
    case 2 :
      result = s6_accessrules_uidgid_cdb(uid, gid, &cdbmap, &params) ; break ;
    default : break ;
  }
  if (result != S6_ACCESSRULES_ALLOW)
  {
    if (verbosity && (result == S6_ACCESSRULES_ERROR))
       strerr_warnw1sys("error while checking rules") ;
    return 0 ;
  }
  if (params.exec.len && verbosity)
  {
    char fmtuid[UID_FMT] ;
    char fmtgid[GID_FMT] ;
    fmtuid[uid_fmt(fmtuid, uid)] = 0 ;
    fmtgid[gid_fmt(fmtgid, gid)] = 0 ;
    strerr_warnw4x("unused exec string in rules for uid ", fmtuid, " gid ", fmtgid) ;
  }
  if (params.env.s)
  {
    size_t n = byte_count(params.env.s, params.env.len, '\0') ;
    char const *envp[n+1] ;
    if (!env_make(envp, n, params.env.s, params.env.len))
    {
      if (verbosity) strerr_warnwu1sys("env_make") ;
      s6_accessrules_params_free(&params) ;
      return 0 ;
    }
    envp[n] = 0 ;
    *perms = parse_env(envp, perms) ;
  }
  s6_accessrules_params_free(&params) ;
  return !!perms ;
}

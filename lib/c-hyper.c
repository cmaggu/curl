/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) 1998 - 2020, Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.haxx.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/

#include "curl_setup.h"

#if !defined(CURL_DISABLE_HTTP) && defined(USE_HYPER)

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#include <hyper.h>
#include "urldata.h"
#include "sendf.h"
#include "transfer.h"

static size_t read_cb(void *userp, hyper_context *ctx,
                      uint8_t *buf, size_t buflen)
{
  struct connectdata *conn = (struct connectdata *)userp;
  struct Curl_easy *data = conn->data;
  CURLcode result;
  size_t nread;

  (void)ctx;

  data->req.upload_fromhere = (char *)buf;
  result = Curl_fillreadbuffer(conn, buflen, &nread);
  if(result == CURLE_AGAIN) {
    /* would block, register interest */
    if(data->hyp.read_waker)
      hyper_waker_free(data->hyp.read_waker);
    data->hyp.read_waker = hyper_context_waker(ctx);
    if(!data->hyp.read_waker) {
      failf(data, "Couldn't make the read hyper_context_waker");
      return HYPER_IO_ERROR;
    }
    return HYPER_IO_PENDING;
  }
  return 0;
}

static size_t write_cb(void *userp, hyper_context *ctx,
                       const uint8_t *buf, size_t buflen)
{
  struct connectdata *conn = (struct connectdata *)userp;
  struct Curl_easy *data = conn->data;
  CURLcode result;

  /* This might be something else than body! */
  result = Curl_client_write(conn, CLIENTWRITE_BODY, (char *)buf, buflen);
  if(result == CURLE_AGAIN) {
    /* would block, register interest */
    if(data->hyp.write_waker)
      hyper_waker_free(data->hyp.write_waker);
    data->hyp.write_waker = hyper_context_waker(ctx);
    if(!data->hyp.write_waker) {
      failf(data, "Couldn't make the write hyper_context_waker");
      return HYPER_IO_ERROR;
    }
    return HYPER_IO_PENDING;
  }
  else if(result)
    return HYPER_IO_ERROR;
  return buflen;
}

/*
 * Curl_http() gets called from the generic multi_do() function when a HTTP
 * request is to be performed. This creates and sends a properly constructed
 * HTTP request.
 */
CURLcode Curl_http(struct connectdata *conn, bool *done)
{
  struct Curl_easy *data = conn->data;
  struct hyptransfer *h = &data->hyp;
  hyper_io *io;
  hyper_clientconn_options *options;

  /* Always consider the DO phase done after this function call, even if there
     may be parts of the request that is not yet sent, since we can deal with
     the rest of the request in the PERFORM phase. */
  *done = TRUE;

  infof(data, "Time for the Hyper dance\n");

  io = hyper_io_new();
  if(!io) {
    failf(data, "Couldn't create hyper IO");
    return CURLE_OUT_OF_MEMORY;
  }
  hyper_io_set_data(io, conn);
  hyper_io_set_read(io, read_cb);
  hyper_io_set_write(io, write_cb);

  /* create an executor to poll futures */
  if(!h->exec) {
    h->exec = hyper_executor_new();
    if(!h->exec) {
      failf(data, "Couldn't create hyper executor");
      return CURLE_OUT_OF_MEMORY;
    }
  }

  options = hyper_clientconn_options_new();
  if(!options) {
    failf(data, "Couldn't create hyper client options");
    return CURLE_OUT_OF_MEMORY;
  }
  hyper_clientconn_options_exec(options, h->exec);

  if(!h->handshake) {
    /* "Both the `io` and the `options` are consumed in this function call" */
    h->handshake = hyper_clientconn_handshake(io, options);
    if(!h->handshake) {
      failf(data, "Couldn't create hyper client handshake");
      return CURLE_OUT_OF_MEMORY;
    }
  }

  if(HYPER_OK != hyper_executor_push(h->exec, h->handshake)) {
    failf(data, "Couldn't hyper_executor_push");
    return CURLE_OUT_OF_MEMORY;
  }

  return CURLE_OK;
}

#endif /* !defined(CURL_DISABLE_HTTP) && defined(USE_HYPER) */

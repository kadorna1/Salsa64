/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Salsa64 — pila de red compartida; ver netstack.h. Basado en el ejemplo
 * network/httptest de PSL1GHT. */
#include "netstack.h"

#include <http/http.h>
#include <http/https.h>
#include <net/net.h>
#include <net/netctl.h>
#include <ppu-lv2.h>
#include <ssl/ssl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mutex.h>
#include <sys/thread.h>
#include <sysmodule/sysmodule.h>

#include "platform/ps3/log.h"

#define HTTP_POOL   0x10000
#define SSL_POOL    0x40000
#define CONN_US     (10 * 1000 * 1000)
#define RECV_US     (15 * 1000 * 1000)

/* ---- La pila de red (se levanta con el primer usuario y se baja con el último) ---- */

/* Módulos del sistema que hacen falta; cada uno se carga solo si no lo
 * estaba y se descarga solo si lo cargó esta pila. La red (libnet) es
 * del logger UDP (platform/ps3/log.c): netInitialize no hace nada si ya está
 * y aquí nunca se llama a netDeinitialize, que le cortaría el log. */
static const sysModuleId net_mods[] = { SYSMODULE_NET, SYSMODULE_NETCTL, SYSMODULE_HTTP, SYSMODULE_HTTP_UTIL,
					SYSMODULE_SSL, SYSMODULE_HTTPS };
#define NET_MODS (sizeof net_mods / sizeof net_mods[0])

static struct {
	void *http_pool, *ssl_pool, *certs;
	httpsData ca;
	bool http, https, ssl, mod[NET_MODS];
} stack;

static sys_lwmutex_t ns_lock;
static bool ns_lock_ok;
static unsigned ns_users;

static void net_down(void)
{
	if (stack.https)
		httpsEnd();
	if (stack.ssl)
		sslEnd();
	if (stack.http)
		httpEnd();
	for (size_t i = NET_MODS; i-- > 0;)
		if (stack.mod[i])
			sysModuleUnload(net_mods[i]);
	free(stack.http_pool);
	free(stack.ssl_pool);
	free(stack.certs);
	memset(&stack, 0, sizeof stack);
}

/* 0 si quedó lista; si no, un código negativo y `what` dice en qué paso. */
static int net_up(const char **what)
{
	int rc;
	u32 cert_size = 0;
	memset(&stack, 0, sizeof stack);
	*what = "módulos de red";
	for (size_t i = 0; i < NET_MODS; i++) {
		if (sysModuleIsLoaded(net_mods[i]) == 0)
			continue;  /* ya estaba (libnet, por el logger) */
		if ((rc = sysModuleLoad(net_mods[i])) < 0)
			return rc;
		stack.mod[i] = true;
	}
	*what = "netInitialize";
	if ((rc = netInitialize()) < 0)
		return rc;
	stack.http_pool = malloc(HTTP_POOL);
	*what = "memoria";
	if (!stack.http_pool)
		return -1;
	*what = "httpInit";
	if ((rc = httpInit(stack.http_pool, HTTP_POOL)) < 0)
		return rc;
	stack.http = true;
	stack.ssl_pool = malloc(SSL_POOL);
	*what = "memoria";
	if (!stack.ssl_pool)
		return -1;
	*what = "sslInit";
	if ((rc = sslInit(stack.ssl_pool, SSL_POOL)) < 0)
		return rc;
	stack.ssl = true;
	*what = "certificados";
	if ((rc = sslCertificateLoader(SSL_LOAD_CERT_ALL, NULL, 0, &cert_size)) < 0)
		return rc;
	stack.certs = malloc(cert_size);
	if (!stack.certs)
		return -1;
	if ((rc = sslCertificateLoader(SSL_LOAD_CERT_ALL, stack.certs, cert_size, NULL)) < 0)
		return rc;
	stack.ca.ptr = stack.certs;
	stack.ca.size = cert_size;
	*what = "httpsInit";
	if ((rc = httpsInit(1, &stack.ca)) < 0)
		return rc;
	stack.https = true;
	return 0;
}

/* `arg` es el `strict` de la petición (netstack.h). */
static int ssl_accept(s32 verErr, sslCert const certs[], int n, const char *host, httpSslId id, void *arg)
{
	(void)certs;
	(void)n;
	(void)id;
	if (verErr) {
		bool strict = arg != NULL;
		salsa64_log("red: certificado de %s no verificable (0x%x): %s", host ? host : "?", (unsigned)verErr,
			    strict ? "se rechaza" : "se sigue");
		return strict ? -1 : 0;
	}
	return 0;
}

int netstack_request(const struct ns_req *r, uint8_t **buf, size_t *len, int *code)
{
	const char *url = r->url;
	size_t cap = r->cap;
	bool post = r->method && !strcmp(r->method, "POST");
	int rc, status = 0, result = -1;
	httpClientId cid = 0;
	httpTransId tid = 0;
	httpUri uri;
	void *pool = NULL;
	u32 need = 0;
	size_t used = 0, room = 0;
	uint8_t *data = NULL;

	*code = 0;
	if ((rc = httpCreateClient(&cid)) < 0) {
		*code = rc;
		goto end;
	}
	httpClientSetConnTimeout(cid, CONN_US);
	httpClientSetRecvTimeout(cid, RECV_US);
	httpClientSetSendTimeout(cid, RECV_US);
	httpClientSetUserAgent(cid, r->user_agent ? r->user_agent : "Salsa64");
	httpClientSetAutoRedirect(cid, 1);
	httpClientSetSslCallback(cid, ssl_accept, r->strict ? (void *)1 : NULL);
	if ((rc = httpUtilParseUri(&uri, url, NULL, 0, &need)) < 0) {
		*code = rc;
		goto end;
	}
	pool = malloc(need);
	if (!pool || (rc = httpUtilParseUri(&uri, url, pool, need, NULL)) < 0) {
		*code = pool ? rc : -1;
		goto end;
	}
	if ((rc = httpCreateTransaction(&tid, cid, post ? HTTP_METHOD_POST : HTTP_METHOD_GET, &uri)) < 0) {
		*code = rc;
		goto end;
	}
	if (post) {
		httpHeader hdr = { "Content-Type", r->content_type ? r->content_type : "application/x-www-form-urlencoded" };
		if ((rc = httpRequestSetContentLength(tid, r->body_len)) < 0 || (rc = httpRequestSetHeader(tid, &hdr)) < 0) {
			*code = rc;
			goto end;
		}
	}
	if ((rc = httpSendRequest(tid, r->body, (u32)r->body_len, NULL)) < 0) {
		*code = rc;
		goto end;
	}
	if ((rc = httpResponseGetStatusCode(tid, &status)) < 0) {
		*code = rc;
		goto end;
	}
	if (status != HTTP_STATUS_CODE_OK) {
		*code = status;
		goto end;
	}
	for (;;) {
		if (r->cancel && *r->cancel) {
			*code = -2;
			goto end;
		}
		if (room - used < 16384) {
			size_t nsz = room ? room * 2 : 65536;
			if (nsz > cap + 16384)
				nsz = cap + 16384;
			if (nsz <= used + 1) {
				*code = -3;  /* más grande de lo permitido */
				goto end;
			}
			uint8_t *n = realloc(data, nsz);
			if (!n) {
				*code = -1;
				goto end;
			}
			data = n;
			room = nsz;
		}
		u32 got = 0;
		rc = httpRecvResponse(tid, (char *)data + used, (u32)(room - used - 1), &got);
		if (rc < 0) {
			*code = rc;
			goto end;
		}
		if (!got)
			break;
		used += got;
		if (used > cap) {
			*code = -3;
			goto end;
		}
	}
	data[used] = '\0';
	*buf = data;
	*len = used;
	data = NULL;
	result = 0;
end:
	if (tid)
		httpDestroyTransaction(tid);
	if (cid)
		httpDestroyClient(cid);
	free(pool);
	free(data);
	return result;
}


/* ---- Cuenta de usuarios de la pila ---- */

void netstack_init(void)
{
	if (ns_lock_ok)
		return;
	sys_lwmutex_attr_t attr = { SYS_LWMUTEX_PROTOCOL_FIFO, SYS_LWMUTEX_ATTR_NOT_RECURSIVE, "netstack" };
	ns_lock_ok = sysLwMutexCreate(&ns_lock, &attr) == 0;
}

int netstack_acquire(const char **what)
{
	static const char *dummy;
	if (!what)
		what = &dummy;
	if (!ns_lock_ok) {
		*what = "mutex";
		return -1;
	}
	sysLwMutexLock(&ns_lock, 0);
	int rc = 0;
	if (ns_users == 0) {
		rc = net_up(what);
		if (rc < 0)
			net_down();
	}
	if (rc >= 0)
		ns_users++;
	sysLwMutexUnlock(&ns_lock);
	return rc;
}

void netstack_release(void)
{
	if (!ns_lock_ok)
		return;
	sysLwMutexLock(&ns_lock, 0);
	if (ns_users && --ns_users == 0)
		net_down();
	sysLwMutexUnlock(&ns_lock);
}

int netstack_online(void)
{
	s32 st = 0;
	bool init = netCtlInit() >= 0;
	int ok = init && netCtlGetState(&st) >= 0 && st == NET_CTL_STATE_IPObtained;
	if (init)
		netCtlTerm();
	return ok;
}

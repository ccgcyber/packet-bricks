/* header declarations */
#include "pkt_engine.h"
/* for prints etc */
#include "pacf_log.h"
/* for string functions */
#include <string.h>
/* for io modules */
#include "io_module.h"
/* for accessing pc_info variable */
#include "main.h"
/* for accessing affinity-related macros */
#include "thread.h"
/*---------------------------------------------------------------------*/
static elist engine_list;
/*---------------------------------------------------------------------*/
/**
 * Load all the IO modules currently available in the system
 */
static inline void
load_io_module(engine *e) {
	TRACE_PKTENGINE_FUNC_START();
	switch(e->iot) {
	case IO_NETMAP:
		e->iom = netmap_module;
		break;
#if 0
	case IO_DPDK:
		e->iom = dpdk_module;
		break;
	case IO_PFRING:
		e->iom = pfring_module;
		break;
	case IO_PSIO:
		e->iom = psio_module;
		break;
	case IO_LINUX:
		e->iom = linuxio_module;
		break;
#endif
	default:
		TRACE_ERR("Control can never reach here!\n");
	}
	TRACE_PKTENGINE_FUNC_END();
	
}
/*---------------------------------------------------------------------*/
static inline engine *
engine_find(const unsigned char *name)
{
	TRACE_PKTENGINE_FUNC_START();
	engine *eng;
	TAILQ_FOREACH(eng, &engine_list, entry) {
		if (!strcmp((char *)name, (char *)eng->name)) {
			TRACE_PKTENGINE_FUNC_END();
			return eng;
		}
	}

	TRACE_PKTENGINE_FUNC_END();
	return NULL;
}
/*---------------------------------------------------------------------*/
static inline void *
thread_start(void *engptr)
{
	TRACE_PKTENGINE_FUNC_START();
	engine *eng = (engine *)engptr;

	if (eng->cpu != -1) {
		/* Affinitizing thread to engine->cpu */
		if (set_affinity(eng->cpu) != 0) {
			TRACE_LOG("Failed to affintiize engine "
				  "%s thread: %p to core %d\n",
				  eng->name, &eng->t, eng->cpu);
		}
	}

	/* Flip the engine to run == 1 */
	eng->run = 1;

	/* call the I/O specific packet receiver/dispatcher */
	eng->iom.callback(eng);

	TRACE_PKTENGINE_FUNC_END();
	return NULL;
}
/*---------------------------------------------------------------------*/
static inline void
engine_run(engine *eng)
{
	TRACE_PKTENGINE_FUNC_START();

	/* Start your engines now */
	TRACE_DEBUG_LOG("Engine %s starting...\n", eng->name);
	if (pthread_create(&eng->t, NULL, thread_start, (void *)eng) != 0) {
		TRACE_PKTENGINE_FUNC_END();
		TRACE_ERR("Can't spawn thread for starting the engine!\n");
	}

	TRACE_PKTENGINE_FUNC_END();
	return;
}
/*---------------------------------------------------------------------*/
inline void
engine_init()
{
	TRACE_PKTENGINE_FUNC_START();

	/* creating the engine list */
	TAILQ_INIT(&engine_list);

	TRACE_PKTENGINE_FUNC_END();
}
/*---------------------------------------------------------------------*/
void
pktengine_new(const unsigned char *name, const unsigned char *type, 
	      const int8_t cpu)
{
	TRACE_PKTENGINE_FUNC_START();
	engine *eng;
	
	eng = engine_find(name);
	if (eng != NULL) {
		TRACE_LOG("Engine with name: %s already exists\n", name);
		TRACE_PKTENGINE_FUNC_END();
		return;
	}
	
	/* add new engine */
	eng = calloc(1, sizeof(engine));
	if (eng == NULL) {
		TRACE_ERR("Can't allocate mem for engine: %s\n", name);
		TRACE_PKTENGINE_FUNC_END();
		return;
	}

	/* duplicating engine name */
	eng->name = (unsigned char *)strdup((char *)name);
	if (eng->name == NULL) {
		free(eng);
		TRACE_ERR("Can't strdup engine name: %s\n", name);
		TRACE_PKTENGINE_FUNC_END();
		return;
	}

	/*
	 * XXX
	 * for the future: maybe we can support more 
	 * I/O engine types??? 
	 * XXX
	 */
#if 0	
	/* selecting pkt engine I/O type */
	if (!strcmp((char *)type, "netmap"))
		eng->iot = IO_NETMAP;
	else if (!strcmp((char *)type, "dpdk"))
		eng->iot = IO_DPDK;
	else if (!strcmp((char *)type, "psio"))
		eng->iot = IO_PSIO;
	else if (!strcmp((char *)type, "pfring"))
		eng->iot = IO_PFRING;
	else if (!strcmp((char *)type, "linux"))
		eng->iot = IO_LINUX;
#endif

	/* default pkt I/O engine is NETMAP for the moment */
	eng->iot = IO_DEFAULT;
	
	/* load the right I/O module */
	load_io_module(eng);

	/* initialize type-specific private context */
	if (eng->iom.init_context(&eng->private_context) == -1) {
		/* if init fails, free up everything */
		if (eng->private_context != NULL)
			free(eng->private_context);
		free(eng->name);
		free(eng);
		TRACE_ERR("Can't create private context for engine: %s\n", 
			  name);
		TRACE_PKTENGINE_FUNC_END();
		return;
	}

	/* setting the cpu no. on which the engine runs (if reqd.) */
	eng->cpu = cpu;

	/* finally add the engine entry in elist */
	TAILQ_INSERT_TAIL(&engine_list, eng, entry);

	UNUSED(type);
	TRACE_PKTENGINE_FUNC_END();
}
/*---------------------------------------------------------------------*/
void
pktengine_delete(const unsigned char *name)
{
	TRACE_PKTENGINE_FUNC_START();
	engine *eng;

	eng = engine_find(name);
	if (eng == NULL) {
		TRACE_LOG("Can't find engine with name: %s\n", 
			  name);
		TRACE_PKTENGINE_FUNC_END();
		return;
	}

	/* check if engine is running */
	if (eng->run != 0) {
		TRACE_LOG("Can't delete engine %s.. it is already running\n",
			  name);
		TRACE_PKTENGINE_FUNC_END();
		return;
	}

	/* check if ifaces have been unlinked */
	eng->iom.unlink_iface((const unsigned char *)"all", eng);

	/* now delete it */
	free(eng->name);
	/* free the private context as well */
	if (eng->private_context != NULL) {
		free(eng->private_context);
		eng->private_context = NULL;
	}
	free(eng);
	TRACE_DEBUG_LOG("Engine %s has successfully been deleted\n",
			name);

	TRACE_PKTENGINE_FUNC_END();
}
/*---------------------------------------------------------------------*/
void pktengine_link_iface(const unsigned char *name, 
			  const unsigned char *iface,
			  const int16_t batch_size)
{
	TRACE_PKTENGINE_FUNC_START();
	engine *eng;
	int ret;
	
	eng = engine_find(name);
	if (eng == NULL) {
		TRACE_LOG("Can't find engine with name: %s\n",
			  name);
		TRACE_PKTENGINE_FUNC_END();
		return;
	}
	
	/* XXX - 
	 * link the interface now...
	 *
	 * If the batch size is not given, then use
	 * the default batch size taken from pc_info
	 *
	 * XXX
	 */
	ret = eng->iom.link_iface(eng->private_context, iface, 
				  (batch_size == -1) ? 
				  pc_info.batch_size : batch_size);
	if (ret == -1) 
		TRACE_LOG("Could not link!!!\n");
	
	TRACE_PKTENGINE_FUNC_END();
}
/*---------------------------------------------------------------------*/
void
pktengine_unlink_iface(const unsigned char *name,
		       const unsigned char *iface)
{
	TRACE_PKTENGINE_FUNC_START();
	/* XXX - to be filled soon */
	engine *eng;
	
	eng = engine_find(name);
	if (eng == NULL) {
		TRACE_LOG("Can't find engine with name: %s\n",
			  name);
		TRACE_PKTENGINE_FUNC_END();
		return;
	}

	eng->iom.unlink_iface(iface, eng);

	TRACE_PKTENGINE_FUNC_END();
}
/*---------------------------------------------------------------------*/
void
pktengine_start(const unsigned char *name)
{
	TRACE_PKTENGINE_FUNC_START();
	engine *eng;
	
	eng = engine_find(name);
	if (eng == NULL) {
		TRACE_LOG("Can't find engine with name: %s\n",
			  name);
		TRACE_PKTENGINE_FUNC_END();
		return;
	}

	/* XXX - start sniffing (to be set-up) */
	engine_run(eng);

	TRACE_PKTENGINE_FUNC_END();
}
/*---------------------------------------------------------------------*/
void
pktengine_stop(const unsigned char *name)
{
	TRACE_PKTENGINE_FUNC_START();
	engine *eng;
	int rc;
	
	eng = engine_find(name);
	if (eng == NULL) {
		TRACE_LOG("Can't find engine with name: %s\n",
			  name);
		TRACE_PKTENGINE_FUNC_END();
		return;
	}

	/* stop sniffing */
	rc = eng->iom.shutdown(eng);
	if (rc == -1) {
		TRACE_LOG("Engine was not running\n");
	}
	
	TRACE_PKTENGINE_FUNC_END();
}
/*---------------------------------------------------------------------*/
void
pktengine_dump_stats(const unsigned char *name)
{
	TRACE_PKTENGINE_FUNC_START();
	engine *eng;

	eng = engine_find(name);
	if (eng == NULL) {
		TRACE_LOG("Can't find engine with name: %s\n",
			  name);
		TRACE_PKTENGINE_FUNC_END();
		return;
	}

	/* Engine stats */
	fprintf(stdout, "---------- ENGINE (%s) STATS------------\n", name);
	fprintf(stdout, "Byte count: %llu\n", (long long unsigned int)eng->byte_count);
	fprintf(stdout, "Packet count: %llu\n", (long long unsigned int)eng->pkt_count);
	fprintf(stdout, "----------------------------------------\n\n");
	TRACE_PKTENGINE_FUNC_END();
}
/*---------------------------------------------------------------------*/

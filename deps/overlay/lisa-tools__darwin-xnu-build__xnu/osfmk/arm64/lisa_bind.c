/*
 * Lisa (SM7325): the CPU topology here is all CLUSTER_TYPE_SMP (no cluster-type in the device
 * tree, no AMP scheduler in this configuration), so nothing steers work to the fast cores:
 * v33's governor log had the four Cortex-A55 cores at 70-90 % while the A78s idled. The window
 * server's rendering thread (the one that submits to the GPU: iokit/Kernel/LisaAdrenoUserClient.cpp)
 * is bound to the 2.4 GHz prime core instead. The binding takes effect at its next switch.
 */
#include <kern/processor.h>
#include <kern/sched_prim.h>
#include <kern/thread.h>

int lisa_bind_current_thread(int cpu);
int
lisa_bind_current_thread(int cpu)
{
	processor_t processor;

	if (cpu < 0 || cpu >= (int)machine_info.logical_cpu_max) {
		return -1;
	}
	processor = cpu_to_processor(cpu);
	if (processor == PROCESSOR_NULL) {
		return -1;
	}
	(void)thread_bind(processor);
	return 0;
}

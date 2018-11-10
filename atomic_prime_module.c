#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/time.h>

DEFINE_SPINLOCK(lock1);
#define NOT_FINISHED 0
#define FINISHED 1

static unsigned long num_threads = 1;
static unsigned long upper_bound = 10;
static int *counters;
static atomic_t *primes;
int position = 0;
atomic_t is_finished;
atomic_t barrier_counter1;
atomic_t barrier_counter2;

static struct task_struct **arr;

struct timeval t;
static unsigned long long timestamp_init = 0;
static unsigned long long timestamp_first_barrier = 0;
static unsigned long long timestamp_second_barrier = 0;

module_param(num_threads, ulong, 0664);
module_param(upper_bound, ulong, 0664);

static inline unsigned long long
get_current_time(void)
{
	struct timespec currtime;
	getnstimeofday(&currtime);
	return (unsigned long long)timespec_to_ns(&currtime);
}

void first_barrier_sync(void){
	atomic_add(1, &barrier_counter1);

	// wait until all threads arrive at the first barrier point:
	while(atomic_read(&barrier_counter1) != num_threads);
	timestamp_first_barrier = get_current_time();
	printk(KERN_ALERT "first synchronization completed\n");
	return;
}

void second_barrier_sync(void){
	atomic_add(1, &barrier_counter2);

	// wait until all threads arrive at the second barrier point:
	while(atomic_read(&barrier_counter2) != num_threads);
	timestamp_second_barrier = get_current_time();
	printk(KERN_ALERT "second synchronization completed\n");

	return;
}

void mark_primes(int *counter){
	int curr_position, i, val;

	// repeatedly performs sieve of eratosthenes
	while(1){
		/*
		* Sets the local position variable and finds the next position variable
		* for the next thread that will get the position variable
		*/
		spin_lock(&lock1);
		curr_position = position;
		val = curr_position + 2;
		position++;
		if(position == upper_bound-1){
			break;
		}
		while(atomic_read(&primes[position]) == 0 && position < upper_bound-1){
			position++;
		}
		spin_unlock(&lock1);

		if(curr_position > upper_bound-2){
			break;
		}

		/*
		* Marks off the multiples of local position variable
		*/
		for(i=curr_position+val; i<upper_bound-1; i+=val){
			atomic_set(&primes[i], 0);
			(*counter)++;
		}
	}
	return;
}

int run(void *counter){
	first_barrier_sync();
	mark_primes((int *)counter);
	second_barrier_sync();
	atomic_set(&is_finished, FINISHED);
	return 0;
}

static int atomic_prime_init(void){
	int i;
	char name[256] = "thread_";
	char num[256];
	timestamp_init = get_current_time();

	printk(KERN_ALERT "prime_module initialization begins...\n");

	// safety initialization:
	primes = 0;
	counters = 0;
	atomic_set(&is_finished, FINISHED);
	atomic_set(&barrier_counter1, NOT_FINISHED);
	atomic_set(&barrier_counter2, NOT_FINISHED);

	if(num_threads < 1 || upper_bound < 2){
		/* error */
		printk(KERN_ALERT "Invalid module parameter\n");
		upper_bound = 0;
		num_threads = 0;
		return -1;
	}

	primes = kmalloc((upper_bound-1)*sizeof(atomic_t), GFP_KERNEL); //FIXED
	if(primes == NULL){
		/* error */
		printk(KERN_ALERT "Not enough memory to allocate\n");
		primes = 0;
		upper_bound = 0;
		num_threads = 0;
		return -2;
	}

	counters = kmalloc((num_threads)*sizeof(int), GFP_KERNEL);
	if(counters == NULL){
		/* error */
		printk(KERN_ALERT "Not enough memory to allocate\n");
		counters = 0;
		kfree(primes);
		primes = 0;
		upper_bound = 0;
		num_threads = 0;
		return -3;
	}

	// initializing per thread counters:
	for(i=0; i<num_threads; i++){
		counters[i] = 0;
	}

	// initializing array of primes:
	for(i=0; i<upper_bound-1; i++){
		atomic_set(&primes[i],i+2);
	}

	// initializing atomic variable:
	atomic_set(&is_finished, NOT_FINISHED);

	// initializing kthreads:
	arr = kmalloc((num_threads)*sizeof(struct task_struct*), GFP_KERNEL);
	for(i=0; i<num_threads; i++){
		sprintf(num, "%d", i);
		strcat(name, num);
		arr[i] = kthread_create(run, (void *)&counters[i], name);
		wake_up_process(arr[i]);
		strcpy(name, "thread_");
	}

	printk(KERN_ALERT "initialization has ended!\n");
	return 0;
}

static void atomic_prime_exit(void){
	int i, count=0, num_nonprimes=0, num_crossed=0;

	if(atomic_read(&is_finished) == NOT_FINISHED){
		printk(KERN_ALERT "Not all processing has completed. Cleaning up threads...\n");
		for(i=0; i<num_threads; i++){
			kthread_stop(arr[i]);
		}
		return;
	}

	printk("Primes: \n");
	for(i=0; i < upper_bound-1; i++){
		if(atomic_read(&primes[i]) != 0){
			printk("%d ", atomic_read(&primes[i]));
			count++;

			if(count % 8 == 0){
				printk("\n");
			}
		}
	}
	printk("\n");

	num_nonprimes = upper_bound - count - 1;
	printk("Number of primes: %d\n", count);
	printk("Number of non-primes: %d\n", num_nonprimes);

	for(i=0; i<num_threads; i++){
		num_crossed += counters[i];
	}
	printk("Unnecessary crosses: %d\n", num_crossed - num_nonprimes);

	printk("num_threads: %lu, upper_bound: %lu\n", num_threads, upper_bound);

	printk("Initialization time: %llu\n", timestamp_first_barrier - timestamp_init);
	printk("Processing time: %llu\n", timestamp_second_barrier - timestamp_first_barrier);

	kfree(primes);
	kfree(counters);

	printk("prime_module has unloaded..\n");
	return;
}

module_init(atomic_prime_init);
module_exit(atomic_prime_exit);


MODULE_LICENSE ("WUSTL");
MODULE_AUTHOR ("Annie Lee, Hakkyung Lee");
MODULE_DESCRIPTION ("CSE422:Lab2 Concurrent Memory Use(atomic)");

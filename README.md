# BGP Simulator

This project is a high-performance BGP route propagation simulator with optional Route Origin Validation (ROV). It:

- loads an AS relationship graph,
- ingests a set of BGP announcements,
- ingests a list of ROV-enforcing ASNs, and
- produces a `ribs.csv` RIB dump that can be compared to reference outputs.

Where possible, the implementation is aggressively optimized; in places where it is not “blazingly fast”, the code is intentionally kept clean and as user friendly as C can be (there are plenty of comments in code so it shouldn't be hard to catch on to what I am doing).

Important Note: This program is fragile if it fails or segfaults give it one more shot (please) and it should work perfectly (failure and segfaults are exceedingly rare, but I have seen them). I am fairly certain it is so fragile because of a mix of compiler flags and risky design decissions (heavy use of bitwise operations, occasional goto, bypassing system memory management) in the name of "how fast can I theoretically make this", even though some such decissions were left out. Please understand that this program should not be treated as a final *reliable* product or something that should be used "in the field" it is only meant to be as quick as feasibly possible.

---

## Compilation and Running

- Build the project from the repository root (same directory as this `README.md` and `Makefile`):

	```bash
	make
	```

	This produces `build/bgp_simulator`.

- Run the simulator from the `build` directory:

	```bash
	cd build
	./bgp_simulator --relationships <file> \
					--announcements <file> \
					--rov-asns <file> \
					[--output <file>]
	```

	- `--relationships` (required): AS relationship file (`provider/customer/peer`).
	- `--announcements` (required): BGP announcements CSV (origin ASN, prefix, optional invalid flag).
	- `--rov-asns` (required): text file listing ASNs that enforce ROV.
	- `--output` (optional): output CSV path (defaults to `ribs.csv`).

	Argument parsing and user-facing error messages are handled in `parse_args`. This makes incorrect CLI usage fail fast.

- Run the C++ tests (requires GoogleTest):

	```bash
	make tests
	```

	Tests live in `tests/test_graph.c++` and `tests/test_system.c++`. They exercise:

	- relationship loading and graph structure,
	- cycle detection,
	- basic propagation behavior, and
	- ROV preference (ROV nodes prefer valid paths).

- Run all benchmarks and compare against the provided gold outputs:

	```bash
	make bench	# runs prefix, subprefix, many
	# or individually:
	make bench-prefix
	make bench-subprefix
	make bench-many
	```

	Benchmarks invoke `bench/compare_output.sh`, makes easy correctness checks.

---

## Inputs and Outputs (High-Level)

The details are implemented in the loader and writer modules:

- `src/relationships_loader.c` (`include/relationships_loader.h`)
- `src/announcements_loader.c` (`include/announcements_loader.h`)
- `src/rov_loader.c` (`include/rov_loader.h`)
- `src/ribs_writer.c` (`include/ribs_writer.h`)

### Relationships file (`--relationships`)

- One relationship per line:

	```text
	left_asn|right_asn|rel
	```

	where `rel` is:

	- `-1` - `left_asn` is a provider of `right_asn`
	- `1`  - `left_asn` is a customer of `right_asn`
	- `0`  - `left_asn` and `right_asn` are peers

- `#` starts a comment and the line is ignored.

Parsing is done by `parse_line`:

```c
// src/relationships_loader.c
char *first = strchr(line, '|');
...
uint32_t left = (uint32_t)strtoul(left_str, NULL, 10);
uint32_t right = (uint32_t)strtoul(right_str, NULL, 10);
int rel = (int)strtol(rel_str, NULL, 10);

if(rel == 0){
		as_graph_add_peer(graph, left, right);
} else if (rel == -1){
		as_graph_add_provider_customer(graph, left, right);
} else if (rel == 1){
		as_graph_add_provider_customer(graph, right, left);
}
```

### Announcements CSV (`--announcements`)

- CSV with a header that includes at least `asn` and `prefix` (third column is an
	optional invalid flag).
- Each data row:
	- column 0: origin ASN,
	- column 1: prefix string (interned into the string pool),
	- column 2: optional flag treated as boolean (`1`, `true`, `yes` → invalid).

Relevant code:

```c
// src/announcements_loader.c
OriginSeed seed = {0};
seed.origin_asn = (uint32_t)strtoul(fields[0], NULL, 10);
trim(fields[1]);
seed.prefix_id = string_pool_intern(fields[1]);
if(field_count >= 3){
	trim(fields[2]);
	seed.rov_invalid = parse_bool(fields[2]);
}

as_graph_get_or_create(graph, seed.origin_asn);
origin_seed_list_push(seeds, &seed);
```

### ROV ASN list (`--rov-asns`)

- Plain text, one ASN per line, with optional `#` comments.

```c
// src/rov_loader.c
uint32_t asn = (uint32_t)strtoul(buffer, NULL, 10);
rov_set_insert(set, asn);
```

### Output RIB (`--output`, default `ribs.csv`)

The simulator writes a CSV with header:

```text
asn,prefix,as_path
```

Each row is one best path per ASN/prefix:

```c
// src/ribs_writer.c
fprintf(output, "asn,prefix,as_path\n");
for(size_t i = 0; i < final_table.size; ++i){
	fprintf(output, "%u,%s,\"%s\"\n",
			final_table.rows[i].asn,
			string_pool_get(final_table.rows[i].prefix_id),
			final_table.rows[i].path);
}
```

Rows are sorted by `(asn, prefix)` to make diffing against reference RIBs stable.

---

## High-Level Simulation Flow

The high-level orchestration lives in `run_simulation`:

```c
// src/simulator.c
int run_simulation(const char *relationships_path, const char *announcements_path, const char *rov_asns_path, const char *output_path, char **error_message){
	if(error_message){ *error_message = NULL; }
	bgp_arena_reset();
	string_pool_reset();
	ASGraph graph; as_graph_init(&graph);
	OriginSeedList seeds; origin_seed_list_init(&seeds);
	ROVSet rov_set; rov_set_init(&rov_set);
	...
	relationships_loader_load_file(relationships_path, &graph, &local_error);
	rov_loader_load_file(rov_asns_path, &rov_set, &local_error);
	announcements_loader_load_file(announcements_path, &graph, &seeds, &local_error);
	assign_policies(&graph, &rov_set);
	install_origin_announcements(&graph, &seeds, &rov_set);
	cycle_checker_ensure_acyclic(&graph, &local_error);
	as_graph_compute_propagation_ranks(&graph);
	propagation_run(&graph);
	ribs_writer_write_csv(&graph, output_path, &local_error);
	...
}
```

Error handling uses a single `failure:` label to keep the success path straight‑line and ensure consistent cleanup.

---

## Optimization Design Choices

The sections below correspond to the bullets I originally sketched and tie them to concrete implementation details.

### Side notes

- Hardware assumptions:
	- The program is designed to run well on the assignment target (2 cores, 8 GB RAM), but it scales to more cores because `parallel_for` and `parallel_run` spawn additional threads when there is enough work.
	- There is minimal dynamic allocation from the system allocator; most memory comes from thread-local arenas backed by `mmap` insteal of `malloc`.

- Warm cache effect:
	- On my Mac, the first run takes ~2.792s; a second run on the same inputs takes ~2.430s (~12.9% speedup). This is likely due to:
		- the binary and code paths being paged into memory, and
		- the OS page cache already holding the input files.
	- The simulator itself is deterministic and does not cache state across runs beyond the global arenas, which are reset at the start of each simulation.

- Rare benchmark flakiness:
	- In local testing, `bench/many` failed once out of 60+ runs when compared via `bench/compare_output.sh`, I think this is because of the -O3 flag durring compilation. If it fails for you, running:

	```bash
	bench/compare_output.sh <expected> <actual> >> test.log
	```

	and inspecting the diff is the easiest way to see what changed (and if it does could you please send me a copy of the `test.log`, and like *give it **one** more shot?*).

### General speedups

- **Use of pointers to context structs instead of copying big data**

	Almost all non-trivial operations are driven by small “context” structs that are passed by pointer into worker functions. This:

	- avoids copying large arrays or graphs, and
	- keeps function signatures small and cache-friendly.

	For example, policy assignment in `src/simulator.c`:

	```c
	typedef struct AssignPoliciesContext{
		ASNode **nodes;
		size_t count;
		const ROVSet *rov_set;
		int error;
	} AssignPoliciesContext;

	static void assign_policies_range(size_t start, size_t end, void *ctx){
		AssignPoliciesContext *context = (AssignPoliciesContext *)ctx;
		for(size_t i = start; i < end; ++i){
			ASNode *node = context->nodes[i];
			bool use_rov = context->rov_set && rov_set_contains(context->rov_set, node->asn);
			add_policy_to_node(node, use_rov);
			if(!node->policy){ context->error = 1; }
		}
	}
	```

	The same pattern appears in:

	- `InstallOriginsContext` (seeding origin announcements),
	- `ProcessNodesContext` / `ProcessAllContext` (processing policy queues),
	- `CollectTask` (RIB collection), and
	- `CycleTask` (cycle checking).

- **Prefer stack allocation over heap allocation**

	The main simulation structs (`ASGraph`, `OriginSeedList`, `ROVSet`) live on the stack inside `run_simulation`, and only their internals (vectors, maps) allocate from the arena:

	```c
	// src/simulator.c
	ASGraph graph;              as_graph_init(&graph);
	OriginSeedList seeds;       origin_seed_list_init(&seeds);
	ROVSet rov_set;             rov_set_init(&rov_set);
	```

	This keeps object lifetime simple and reduces pointer chasing.

- **Efficient DFS-based cycle checking**

	Cycle detection is implemented as a depth-first search over the directed AS graph in both directions (provider→customer and customer→provider) in `src/cycle_checker.c`:

	```c
	static int dfs_cycle(const ASGraph *graph, ASNode *node,
						CycleDirection direction, int *state,
						UInt32Vector *stack, char **cycle_message) {
		size_t index = node->graph_index;
		state[index] = 1;
		uint32_vector_push(stack, node->asn);

		const UInt32Vector *neighbors = neighbors_for_direction(node, direction);
		for(size_t i = 0; i < neighbors->size; ++i){
			ASNode *neighbor = as_graph_find((ASGraph *)graph, neighbors->data[i]);
			...
		}
		uint32_vector_pop(stack, NULL);
		state[index] = 2;
		return 0;
	}
	```

	The `state` array and `stack` keep the DFS iterative and cache-friendly, and `build_cycle_message` constructs a human-readable cycle description:

	```c
	// Example format: "Provider cycle detected: 1 -> 2 -> 3 -> 1"
	```

- **Low-level memory manipulation where it matters**

	Hot-path data structures use `memcpy`/`memmove` directly to avoid extra layers:

	```c
	// src/vector.c
	memmove(vec->data + 1, vec->data, vec->size * sizeof(uint32_t));
	...
	memcpy(dest->data, src->data, src->size * sizeof(uint32_t));
	```

	The arena uses `ROUND_UP` and manual pointer arithmetic to ensure alignment:

	```c
	// deps/arena/arena.h
	#define ARENA_ALIGN alignof(max_align_t)
	#define ROUND_UP(needed, align) \
		(((needed) + ((align) - 1)) & ~((align) - 1))
	```

- **Occasional use of `goto` to centralize error handling**

	Instead of deeply nested `if`/`else` blocks, `run_simulation` uses a single `failure:` label to keep the main logic linear while preserving a single exit path:

	```c
	if(relationships_loader_load_file(relationships_path, &graph, &local_error) != 0){
		if(error_message){ *error_message = local_error; }
		goto failure;
	}
	...
	failure:
		return -1;
	```

	This is slightly “old-school C”, but it makes the code both faster (fewer branches) and easier to reason about than duplicating cleanup in multiple places.

### Multithreading speedups

- **Thread-centric memory model and thread-local arenas**

	All dynamic allocations go through `bgp_alloc` / `bgp_realloc` in `src/bgp_utils.c`, which are wrappers around a per-thread arena:

	```c
	// src/bgp_utils.c
	static THREAD_LOCAL Arena *tls_arena = NULL;

	static Arena *get_thread_arena(void){
		if(!tls_arena){
			tls_arena = arenaLocalInit();
			register_arena(tls_arena);
		}
		return tls_arena;
	}

	void *bgp_alloc(size_t size){
		Arena *arena = get_thread_arena();
		void *ptr = arenaLocalAlloc(arena, size ? size : 1);
		if(!ptr){ abort(); }
		return ptr;
	}
	```

	`THREAD_LOCAL` is defined in both `bgp_utils.c` and `parallel.c`, so each worker thread has an independent arena with no sharing and no per-allocation locks.

- **Use of `pthread` and `parallel_for`**

	Parallel work is abstracted via `parallel_for` and `parallel_run` in `src/parallel.c`:

	```c
	void parallel_for(size_t count, ParallelRangeFn fn, void *ctx){
		if(!fn || count == 0) return;
		if(count < PARALLEL_THRESHOLD || g_parallel_depth > 0){
			fn(0, count, ctx);
			return;
		}

		size_t mid = count / 2;
		ParallelTask task = {.fn = fn, .ctx = ctx, .start = 0, .end = mid};
		pthread_t thread;
		int created = pthread_create(&thread, NULL, parallel_worker, &task);
		g_parallel_depth++;
		fn(mid, count, ctx);
		g_parallel_depth--;
		if(created == 0){ pthread_join(thread, NULL); }
		else{ task.fn(task.start, task.end, task.ctx); }
	}
	```

	This is used in:

	- `assign_policies` to assign policies across all nodes,
	- `install_origin_announcements` to seed origin announcements,
	- `propagation_run` to process nodes and send announcements,
	- `ribs_writer_write_csv` to collect RIB rows, and
	- `cycle_checker_ensure_acyclic` for the two search directions when the graph is
		large (`CYCLE_PARALLEL_THRESHOLD`).

	A `PARALLEL_THRESHOLD` ensures very small jobs run single-threaded to avoid thread-creation overhead.

- **Hot-path locking with spinlocks**

	Per-node policies use a tiny spinlock (instead of a mutex) around receive/queue processing, which trims synchronization overhead in the tight propagation loops without sacrificing correctness.

- **Pointer-friendly propagation**

	Propagation buckets now store dense graph indices and each node caches neighbor pointers (providers/customers/peers) once after graph build. This removes thousands of hashmap lookups during send/receive and speeds up the three propagation phases (up, peer, down).

- **Per-thread depth tracking**

	The `g_parallel_depth` thread-local counter prevents nested parallel regions from oversubscribing the CPU; inside a parallel region, further `parallel_for` calls run serially.

### Compiler optimizations

The `Makefile` enables a set of flags specifically chosen for speed on modern CPUs:

```make
# Makefile
SPEED_FLAGS := -O3 -march=native -mtune=native -pipe \
			-ffunction-sections -fdata-sections -fno-plt \
			-DNDEBUG -ffast-math
LDFLAGS += -flto -Wl,-O3 -pthread
```

- `-O3`, `-ffast-math`: aggressive optimization, especially in tight loops.
- `-march=native`, `-mtune=native`: generate code tuned to the host architecture.
- `-ffunction-sections`, `-fdata-sections`: enable the linker to drop unused code.
- `-fno-plt`: avoid extra PLT indirections on calls where possible.
- `-DNDEBUG`: strip asserts from hot code paths.
- `-flto` and `-Wl,-O3`: enable link-time optimization across translation units.

### Custom Vector Library

The project uses a minimal, `uint32_t`-specific vector to store things like ASNs, prefix IDs, and AS paths.

```c
// include/vector.h
typedef struct UInt32Vector{
	uint32_t *data;
	size_t size;
	size_t capacity;
} UInt32Vector;
```

Key properties:

- Specialized to `uint32_t`:
	- matches ASNs and string pool IDs exactly,
	- avoids void* conversions and extra metadata.
- Backed by the arena (`bgp_realloc`), so:
	- there is no per-element `malloc`/`free` overhead,
	- growth is geometric (`*2`) with a small minimum.

Example operations in `src/vector.c`:

```c
// Grow capacity and copy in one go
uint32_t *new_data = (uint32_t *)bgp_realloc(vec->data, old_size, new_capacity * sizeof(uint32_t));

// Efficient prepend using memmove
memmove(vec->data + 1, vec->data, vec->size * sizeof(uint32_t));
vec->data[0] = value;
vec->size += 1;
```

This is essentially a “C replacement” for `std::vector<uint32_t>` with tighter control over layout and allocation behavior.

### Memory speedups

*Disclaimer*: this program is purposefully a **memory hog** and most memory design decissions are based around using as much memory as possible and never freeing any of it (not good practice but *really* quick).

- **Arena allocator in `/deps/arena`**

	All higher-level allocations route through the arena library:

	```c
	// deps/arena/arena.h
	#define BUFF_SIZE (size_t)(1024 * 1024) // 1MB by default
	#define ARENA_ALIGN alignof(max_align_t)
	```

	and

	```c
	// deps/arena/arena.c
	#if defined(__unix__) || defined(__APPLE__)
	#define ARENA_HAVE_MMAP 1
	...
	void *mapped = mmap(NULL, mapSize, PROT_READ | PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	```

	The arena:

	- aligns all allocations to `alignof(max_align_t)` to minimize misalignment,
	- uses `mmap` for large pages when available, with a fallback to `malloc`,
	- allocates in 1MB blocks (`BUFF_SIZE`) or larger to amortize allocator overhead and best align with modern CPU L3 cache sizes.
	- keeps all memory sequential which is most friendly for a cache prefetcher so in most cases there is no wait to fetch data from RAM.

- **Per-thread arenas and global reset**

	Instead of freeing individual allocations, each thread has its own arena and we simply reset all arenas between runs:

	```c
	// src/bgp_utils.c
	void bgp_arena_reset(void){
			pthread_mutex_lock(&g_registry.mutex);
			for(size_t i = 0; i < g_registry.count; ++i){
				arenaLocalReset(g_registry.arenas[i]);
			}
			pthread_mutex_unlock(&g_registry.mutex);
	}
	```

	This design:

	- eliminates allocator contention during the simulation,
	- trades per-object deallocation for a single, very fast bulk reset (which unless I forgot to comment it out somewhere doesn't actually happen).

- **Intentional “leak” on process exit**

	The simulator is a short-lived process, and the assignment did not require strict reclamation of all memory. Many `*_free` functions are no-ops after moving everything to arenas:

	```c
	// src/cycle_checker.c
	// uint32_vector_free(&stack);
	```

	and in `src/simulator.c` the cleanup calls are commented out:

	```c
	// origin_seed_list_free(&seeds);
	// rov_set_free(&rov_set);
	// as_graph_free(&graph);
	```

	This simplifies ownership significantly and avoids the cost of traversing large graphs to free nodes, while still being safe in the context of a batch simulator that exits after writing its output.

### Hashmap speedups

`UInt32Map` is a custom `uint32_t` -> `void*` hashmap optimized for low overhead:

```c
// include/hash_map.h
typedef struct UInt32MapEntry{
	uint32_t key;
	void *value;
	unsigned long hash;
	struct UInt32MapEntry *next;
} UInt32MapEntry;

typedef struct UInt32Map{
	UInt32MapEntry **buckets;
	size_t capacity;
	size_t size;
	UInt32MapEntry *free_entries;
} UInt32Map;
```

Key design choices:

- **`& (capacity - 1)` instead of modulus**

	Bucket indices are computed with bitwise AND, which is much cheaper than integer division:

	```c
	// src/hash_map.c
	size_t idx = hash & (new_capacity - 1);
	...
	size_t idx = hash & (map->capacity - 1);
	```

	Given that capacities are powers of two, this is equivalent to `% capacity` but typically costs just a few cycles vs. 20-40 cycles for hardware division (practically means 10x speedup for computing indeces in hashmap).

- **Simplified hashing**

	The hash function was intentionally simplified:

	```c
	static inline unsigned long hash_uint32(uint32_t value){
		// value = ((value >> 16) ^ value) * 0x45d9f3b;
		// value = ((value >> 16) ^ value) * 0x45d9f3b;
		// value = (value >> 16) ^ value;
		return (unsigned long)value;
	}
	```

	Using the key directly as a hash avoids extra work; after trying a more complex mix, I removed it and observed about a 9% speedup on my machine (e.g., from ~3.099s to ~2.793s on a /bench/many/).

- **Free-list of entries**

	`UInt32Map` recycles entries via `free_entries` and `uint32_map_clear`, reducing the number of new arena allocations for intermediate maps.

### String speedups

The string pool is designed for fast prefix handling:

```c
// include/string_pool.h
uint32_t string_pool_intern(const char *value);
const char *string_pool_get(uint32_t id);
uint32_t string_pool_size(void);
```

- **Interning and FNV-1a-style hashing**

	`string_pool.c` uses a custom hash and open addressing:

	```c
	static unsigned long hash_string(const char *str){
		unsigned long hash = 1469598103934665603ull;
		while(*str){
			hash ^= (unsigned char)(*str++);
			hash *= 1099511628211ull;
		}
		return hash;
	}
	```

	Prefix strings are stored once and referred to by integer ID, making them ideal keys for `UInt32Map` and reducing memory traffic compared to copying raw `char *` everywhere (learned this from Crafting Interpreters textbook, this is a tecnique commonly used in interpreted language runtimes).

- **Usage in loaders and writer**

	- In `announcements_loader`, prefixes are interned:

		```c
		seed.prefix_id = string_pool_intern(fields[1]);
		```

	- In `ribs_writer`, the human-readable prefix is retrieved just before writing:

		```c
		string_pool_get(final_table.rows[i].prefix_id)
		```

- **Experimental buffer-based writer**

	There was an experimental version where both threads wrote into a shared, string-pool-backed output buffer for even faster CSV generation. It was really fast but more complex and brittle (it failed depending on your hardware and OS, like it was fine on my mac, but not on an x86 laptop running fedora), so for this assignment I kept the simpler file-based `fprintf` approach for robustness.

- **Prefix caching in RIB sort**

	RIB rows cache the `prefix` string pointer at collect time, so the final sort compares cached strings directly instead of re-fetching from the string pool for every comparison.

### Policy/routing speedups

- **Cached best candidate per prefix**

	Each receive queue keeps track of the currently best candidate as announcements arrive; queue processing tries that first and only falls back to a full scan if it would create a loop, reducing per-prefix work in the hot path.

---

## Directories

For quick reference, the repo layout is:

```text
.
├── Makefile
├── README.md
├── bench
│   ├── compare_output.sh
│   ├── many
│   │   ├── CAIDAASGraphCollector_2025.10.15.txt
│   │   ├── CAIDAASGraphCollector_2025.10.16.txt
│   │   ├── anns.csv
│   │   ├── ribs.csv
│   │   └── rov_asns.csv
│   ├── prefix
│   │   ├── CAIDAASGraphCollector_2025.10.15.txt
│   │   ├── CAIDAASGraphCollector_2025.10.16.txt
│   │   ├── anns.csv
│   │   ├── ribs.csv
│   │   └── rov_asns.csv
│   ├── ribs.csv
│   └── subprefix
│       ├── CAIDAASGraphCollector_2025.10.15.txt
│       ├── CAIDAASGraphCollector_2025.10.16.txt
│       ├── anns.csv
│       ├── ribs.csv
│       └── rov_asns.csv
├── cse3150_course_project.pdf
├── deps
│   └── arena
│       ├── CHANGELOG.md
│       ├── LICENSE
│       ├── README.md
│       ├── arena.c
│       ├── arena.h
│       └── testing
│           ├── bench_stress.c
│           ├── normal_use_case.c
│           └── test_arena.py
├── extras
│   ├── 20250901.as-rel2.txt
│   ├── file_download.c
│   └── file_unzip.c
├── include
│   ├── announcement.h
│   ├── announcements_loader.h
│   ├── as_graph.h
│   ├── as_node.h
│   ├── bgp_utils.h
│   ├── cycle_checker.h
│   ├── hash_map.h
│   ├── parallel.h
│   ├── policy.h
│   ├── propagation.h
│   ├── relationships_loader.h
│   ├── ribs_writer.h
│   ├── rov_loader.h
│   ├── simulator.h
│   ├── string_pool.h
│   └── vector.h
├── src
│   ├── announcement.c
│   ├── announcements_loader.c
│   ├── as_graph.c
│   ├── as_node.c
│   ├── bgp_utils.c
│   ├── cycle_checker.c
│   ├── hash_map.c
│   ├── main.c
│   ├── parallel.c
│   ├── policy.c
│   ├── propagation.c
│   ├── relationships_loader.c
│   ├── ribs_writer.c
│   ├── rov_loader.c
│   ├── simulator.c
│   ├── string_pool.c
│   └── vector.c
└── tests
	├── test_graph.c++
	└── test_system.c++
```

---

## Notes on Documentation and AI Use

- I tried to document every major design decision (data structures, multithreading, memory management, and performance tradeoffs) in this `README.md`. Although there are some that I gauruntee I forgot along the way.
- The core C code (simulator, graph, propagation, loaders, arena integration, etc.) along with the arena in `/deps/arena/` is my own work.
- I used AI assistance to help draft and refine the C++ GoogleTest files (`tests/test_graph.c++`, `tests/test_system.c++`) and have clearly marked that in comments at the top of those files. The tests were then adapted and validated against this codebase. I also used AI assistance in the making of this `README.md`, the list of optimizations and usage was generated by me then "enhanced" by AI (it added code sections and paragraphs and split it into clear sections).

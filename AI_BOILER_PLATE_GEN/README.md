# Address Translation Boilerplate

This directory is a small, self-contained starting point for experimenting
with virtual-to-physical address translation and TLB behavior in gem5. It
contains both an ordinary C++17 program that can be compiled without gem5 and
a gem5-style `SimObject` that exposes the same basic mapping through Python
parameters.

The model deliberately favors transparency over architectural completeness. A
virtual page is translated by adding a fixed offset to its page number, and a
direct-mapped TLB remembers recently translated pages. There is no page table,
permission checking, page fault handling, replacement policy selection, or
memory-system port.

## Directory contents

| File | Purpose |
| --- | --- |
| `short_address_translation.cc` | Standalone C++17 example for learning and quick smoke tests. |
| `traffic_generator.cc` | Standalone seeded random-address workload with TLB timing and statistics. |
| `address_translation.hh` | C++ `AddressTranslation` declaration and public API. |
| `address_translation.cc` | Translation logic, validation, TLB state, and statistics. |
| `AddressTranslation.py` | gem5 `SimObject` declaration and user-facing parameters. |
| `config_address_translation.py` | Minimal Python configuration example. |
| `SConscript` | Registers the Python SimObject and C++ source with gem5's build system. |
| `README.md` | Usage, integration, behavior, and extension notes. |

## Concepts and address calculation

The model splits an address into a virtual page number and an offset within
that page. With `page_bits = 12`, the page size is $2^{12} = 4096$ bytes:

```text
virtual_page  = virtual_address >> page_bits
page_offset   = virtual_address & (page_size - 1)
physical_page = virtual_page + physical_offset
physical_addr = (physical_page << page_bits) | page_offset
```

For example, with `virtual_address = 0x2345`, `page_bits = 12`, and
`physical_offset = 0x1000`:

```text
virtual_page  = 0x2
page_offset   = 0x345
physical_page = 0x1002
physical_addr = 0x1002345
```

The offset is added to the page number, not directly to the byte address. This
preserves the page offset during translation. The mapping is deterministic:
the same virtual page always maps to the same physical page until parameters
are changed or the object is reset.

## Standalone C++ example

The short example has no gem5 dependencies, so it is useful for checking the
core address calculation or demonstrating the model to someone who does not
need a complete gem5 build.

### Build and run on Linux, macOS, or WSL

```sh
c++ -std=c++17 -Wall -Wextra -pedantic \
   short_address_translation.cc -o short_address_translation
./short_address_translation
```

On Windows, the same command works from a developer shell with a C++17
compiler such as `clang++` or `g++`; replace `c++` with the compiler command
available in that shell if necessary.

Expected output:

```text
PA=0x1002345 hit=true
```

The standalone program reports `hit=true` because it performs the mapping
directly and does not implement a TLB lookup. TLB hit and miss behavior is
provided by the gem5 `SimObject` described below.

The short example also supports optional run dumps for its single translation:

```sh
./short_address_translation \
  --address 0x2345 \
  --page-size 4096 \
  --physical-offset 0x1000 \
  --log-file short-translation.log
```

`--log-file` writes one combined, sectioned dump containing configuration,
the translation record, and a summary. The file is overwritten on each run.
Use `--help` for the complete list of options. This keeps the short program
useful for a single-address smoke test while `traffic_generator.cc` handles
larger random workloads.

## Random traffic generator

`traffic_generator.cc` generates virtual addresses from an inclusive address
range, sends each address through a direct-mapped TLB model, and charges a
configurable latency for the result. It is independent of gem5, so it can be
compiled and used while iterating on a workload pattern.

The default timing is:

| Outcome | Default cost |
| --- | ---: |
| TLB hit | `4` cycles |
| TLB miss | `30` cycles |

The generator uses `std::mt19937_64`. Supply `--seed` to make a run
reproducible. The default address range is `0x0` through `0xffff`, which
covers 16 KiB with 4 KiB pages. That range contains four pages, so it can show
both reuse and direct-map conflicts with a small TLB.

### Build and run

```sh
c++ -std=c++17 -Wall -Wextra -pedantic \
  traffic_generator.cc -o traffic_generator
./traffic_generator --requests 32 --seed 12345
```

Use `--verbose` to print every generated request and its result:

```sh
./traffic_generator \
  --requests 8 \
  --start 0x0 \
  --end 0x3fff \
  --entries 4 \
  --hit-latency 4 \
  --miss-latency 30 \
  --seed 7 \
  --verbose
```

Each verbose line reports the request number, virtual address, physical
address, hit/miss result, and charged latency. The final summary reports hit
count, miss count, hit rate, total latency, and average latency.

### Combined run log

Use the single `--log-file` option to save configuration, every detailed
request, and aggregate results in one sectioned file. The file is overwritten
on each run, which keeps a result path from accidentally mixing records from
different seeds or configurations:

```sh
./traffic_generator \
  --requests 1000 \
  --start 0x0 \
  --end 0xffff \
  --seed 42 \
  --log-file traffic-run.log
```

The combined file contains these sections:

```text
=== CONFIGURATION ===
...

=== REQUESTS ===
...

=== SUMMARY ===
...
```

The `REQUESTS` section contains one record per generated address:

```text
request=0 VA=0xc11 PA=0x1000c11 miss latency=30 cycles
request=1 VA=0xf30 PA=0x1000f30 hit latency=4 cycles
```

The `SUMMARY` section contains the aggregate report:

```text
Traffic summary
  requests: 1000
  hits: 742
  misses: 258
  hit rate: 74.20%
  total latency: 10732 cycles
  average latency: 10.73 cycles
```

The combined log is independent of `--verbose`: specify `--log-file` when you
want all records saved to disk without printing every request to the terminal.
If the path cannot be opened, the generator exits with an error instead of
silently losing the dump. The parent directory must already exist.

### Command-line options

| Option | Default | Description |
| --- | ---: | --- |
| `--requests N` | `32` | Number of random addresses to generate. `0` is allowed. |
| `--start ADDRESS` | `0x0` | Inclusive beginning of the virtual address range. |
| `--end ADDRESS` | `0xffff` | Inclusive end of the virtual address range. |
| `--seed N` | `12345` | Seed for reproducible random traffic. |
| `--page-bits N` | `12` | Log2 of the page size. Must be in `[1, 62]`. |
| `--entries N` | `4` | Direct-mapped TLB entries. Must be a power of two. |
| `--hit-latency N` | `4` | Cycles charged for a TLB hit. |
| `--miss-latency N` | `30` | Cycles charged for a TLB miss. |
| `--verbose` | off | Print one line for every generated request. |
| `--log-file PATH` | none | Write configuration, requests, and summary to one file. |
| `--help` | n/a | Print usage information. |

Addresses may be written in decimal or with a `0x` hexadecimal prefix. The
range is inclusive, so `--start 0x0 --end 0xfff` generates addresses from the
first 4 KiB page only. That is a useful sanity test: the first request should
miss and subsequent requests should hit, giving total latency
`30 + (request_count - 1) * 4` when at least one request is generated.

The generator rejects a reversed range, an invalid page-bit count, and a TLB
entry count that is not a non-zero power of two. These checks mirror the
constraints in the gem5 model.

## gem5 model

`AddressTranslation` derives from gem5's `SimObject`. Its Python declaration
generates `AddressTranslationParams`, which are consumed by the C++ constructor.
The default values are defined in `AddressTranslation.py`; the configuration
example uses a larger 64-entry TLB with 4-cycle hits and 30-cycle misses.

### Parameters

| Parameter | Default | Meaning and constraints |
| --- | ---: | --- |
| `page_bits` | `12` | Log2 of the page size. Must be from `1` through `62`; `12` means 4 KiB pages. |
| `physical_offset` | `0x1000` | Value added to each virtual page number to produce its physical page number. |
| `tlb_entries` | `16` | Number of direct-mapped entries. Must be a non-zero power of two, such as `1`, `16`, or `64`. |
| `hit_latency` | `4` | Returned latency for a TLB hit, in gem5 cycles. |
| `miss_latency` | `30` | Returned latency for a TLB miss, in gem5 cycles. |

The values are validated when the C++ object is constructed. Invalid
`page_bits` or a non-power-of-two `tlb_entries` causes gem5's `fatal()` error
path rather than creating an unusable object.

### Adding it to a gem5 build

The included `SConscript` registers `AddressTranslation.py` and
`address_translation.cc`, but gem5 only processes an `SConscript` when its
directory is included by the relevant build hierarchy. To integrate this
folder:

1. Add the directory to the appropriate parent `SConscript`, or move the files
  into a selected subsystem below `src/`.
2. Keep `address_translation.hh`, `address_translation.cc`, and
  `AddressTranslation.py` together unless you also update their include and
  registration paths.
3. If the files are moved, update `cxx_header` in `AddressTranslation.py` and
  the include at the top of `address_translation.cc` to match the new path.
4. Build the target gem5 binary so that the generated
  `AddressTranslationParams` header is available.

The current header path is:

```text
AI_BOILER_PLATE_GEN/address_translation.hh
```

That path is intentionally suitable for this folder's current location. It
may need to change when the model becomes part of a production subsystem.

### Python configuration example

`config_address_translation.py` creates the object with explicit values:

```python
from m5.objects import AddressTranslation

translation = AddressTranslation(
   page_bits=12,
   physical_offset=0x1000,
   tlb_entries=64,
  hit_latency=4,
  miss_latency=30,
)

print(translation)
```

This file is a parameter-construction example, not a complete gem5 simulation
configuration. A runnable simulation would also need a root object, clock and
voltage domains as appropriate, and any CPU, memory, and workload objects that
use the translator.

## TLB behavior

The C++ implementation uses a direct-mapped table. For a virtual page number
`virtual_page`, the entry is selected by:

```text
index = virtual_page & (tlb_entries - 1)
```

An access is a hit only when the selected entry is valid and contains the same
virtual page number. On a miss, the selected entry is overwritten immediately
with the new mapping. Consequently, two virtual pages whose low index bits are
equal will evict one another even if other entries are unused.

For example, with four entries, virtual pages `0x01` and `0x05` both select
index `1`. Accessing them alternately produces misses because they conflict in
the same direct-mapped slot. This makes the model useful for controlled
experiments with capacity and conflict behavior.

Each call to `translate()` returns a `TranslationResult` containing:

| Field | Description |
| --- | --- |
| `physicalAddress` | Translated byte address, including the original page offset. |
| `hit` | `true` when the selected TLB entry already matched the virtual page. |
| `latency` | `hit_latency` for a hit or `miss_latency` for a miss. |

The object also tracks cumulative `hits()` and `misses()` counters. Calling
`reset()` invalidates every TLB entry and clears both counters. `dumpState()`
returns a compact diagnostic string containing the entry count and statistics.

## C++ API

The public API is intentionally small:

```cpp
AddressTranslation::TranslationResult translate(Addr virtual_address);
void reset();
std::string dumpState() const;
uint64_t hits() const;
uint64_t misses() const;
```

The object owns its TLB entries in a `std::vector`. Translation updates the
selected entry and statistics synchronously. It does not schedule gem5
events, create ports, or send requests to memory, so callers are responsible
for using the returned latency in whatever timing model they are testing.

## Limitations and intentional simplifications

This is educational boilerplate, not a complete virtual-memory subsystem. In
particular, it currently does not provide:

- page-table walks or multi-level page tables;
- ASIDs, VMIDs, process isolation, or address-space tags;
- page permissions, privilege checks, dirty/accessed bits, or faults;
- configurable associativity or an LRU/random replacement policy;
- multiple page sizes or superpages;
- TLB ports, request packets, event scheduling, or memory traffic;
- checkpoint/restore serialization of entries and counters;
- overflow checks for deliberately extreme mapping parameters.

These omissions keep the control flow easy to inspect. Add the missing
behavior only when the experiment requires it, and define the timing and
state semantics before extending the interface.

## Testing and verification

At minimum, run the standalone smoke test after changing the mapping logic:

```sh
c++ -std=c++17 -Wall -Wextra -pedantic \
   short_address_translation.cc -o short_address_translation
./short_address_translation
```

After changing traffic behavior or timing, build and run the generator with a
single-page range:

```sh
c++ -std=c++17 -Wall -Wextra -pedantic \
  traffic_generator.cc -o traffic_generator
./traffic_generator --requests 4 --start 0x0 --end 0xfff \
  --hit-latency 4 --miss-latency 30 --verbose \
  --log-file traffic-test.log
```

The expected pattern is one miss at 30 cycles followed by three hits at 4
cycles, for 42 total cycles. The exact generated addresses depend on the
seed, but all addresses in this range belong to the same page. The command
also creates one combined dump; remove it after a smoke test if it is not
intended as a saved result.

For the gem5 object, useful focused checks include:

1. Translate two addresses in the same page and confirm that the second access
  hits while preserving its byte offset.
2. Translate pages with the same direct-map index and confirm that the later
  access replaces the earlier entry.
3. Confirm that `reset()` makes the next access a miss and clears both counters.
4. Try `tlb_entries=3` and `page_bits=0` to verify that configuration errors
  are rejected.
5. Check that returned latencies are exactly `hit_latency` and `miss_latency`
  for their corresponding outcomes.
6. Run the traffic generator twice with the same `--seed` and confirm that the
  verbose request sequence and summary are identical.

When this model is moved into a full gem5 build, add a small unit or regression
test around these cases so future changes cannot silently alter the mapping or
replacement behavior.

## Troubleshooting

### `AddressTranslationParams.hh` cannot be found

The SimObject Python file has not been processed by the gem5 build, or the
directory is not reachable from the selected parent `SConscript`. Check the
registration path and rebuild the target.

### The header include cannot be found

The source include and `cxx_header` must describe the same path relative to
gem5's include configuration. Update both when relocating the model.

### A configuration fails with a power-of-two error

`tlb_entries` is used with a bit mask, so values such as `3`, `6`, and `10`
are invalid. Choose a non-zero power of two.

### The physical address is larger than expected

`physical_offset` is applied to the page number. With 4 KiB pages, an offset
of `0x1000` moves the physical mapping by `0x1000 * 0x1000` bytes, while the
page offset remains unchanged. Use a smaller page-number offset if a smaller
byte-address displacement is intended.

## Extending the example

Good next steps for experiments include adding a configurable associativity,
recording per-index conflict statistics, modeling a page-walk latency on a
miss, or introducing a small page-table abstraction. Each extension should
preserve the distinction between translation result, TLB state, and timing so
that the model remains straightforward to test.
# Warden personality endpoint bounded capacity (boot regression)

Base commit: `b2eb17f1c5c0e57a57f6c672c793cc3638d0c2a9`.

## Finding

`apps::setup` requested `n_clients + 1` queue entries. The current static Linux program roster contains 7 Linux clients plus 3 native clients, so it requested 11 entries from a capability endpoint whose maximum is 8. The allocator's `OutOfBudget` refusal was correct; the following `expect("personality endpoint")` panicked and prevented any QEMU guest test from running.

## Fix

Cap the shared personality endpoint queue to `ENDPOINT_MAX_QUEUE`, while keeping its size no larger than the number of clients. This avoids allocating outside the declared upper bound; it does **not** increase any capability budget or the kernel's queue ceiling.

## Limitations

Up to 10 clients can share an 8-slot queue, so bursts can be backpressured with EAGAIN. Consumers and callers must handle that correctly; it is not evidence that all client workloads run successfully or that no work is lost. The existing `forward` path returns EAGAIN when the server queue is full; real workloads must be observed.

## Acceptance

Run the dedicated Python structural regression and `bash tests/kernel/run.sh` in QEMU. Confirm boot proceeds beyond personality endpoint creation; all guest checks and BusyBox workload results must then be assessed separately. Obtain independent review for any remaining queue accounting, retries, or starvation behavior. The target PC remains unverified until separately tested.

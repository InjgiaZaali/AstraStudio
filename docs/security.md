# Security model and limitations

Astra 0.1 is an **educational execution engine** intended for trusted local scripts. It is **not a security sandbox**.

## Implemented protections

- Binds HTTP to `127.0.0.1`, not to public network interfaces
- Routes to only three static assets and two code-related API endpoints plus a health check
- Rejects unexpected Host and Origin header values
- Max 128 KiB source input per request
- Max 1,000,000 VM instructions per run
- Max 256 nested calls
- Max 1 MiB accumulated console output
- Max 1 MiB string concatenation results
- Max 10,000 operand stack values
- No filesystem, networking, process spawning, or environment-variable access from Astra scripts
- HTML editor content is escaped before syntax highlighting, and console outputs use text-only DOM APIs

## Not guaranteed

- In-process memory and CPU isolation. An adversarial program may cause large allocation patterns.
- HTTP-grade traffic hardening, rate limiting, authentication or TLS; **never expose this service publicly**.
- Cross-tab concurrency, graceful shutdown or crash recovery. The server serves one connection at a time.
- Secure execution of arbitrary downloaded Astra code.
- Source-level debugging or garbage collection.

For robust untrusted-code execution, compile/run in an isolated subprocess with OS sandboxing, memory/time budgets, and a hardened IPC protocol. That is future work.

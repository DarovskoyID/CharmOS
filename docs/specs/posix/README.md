# Semaphore references

`sem_wait.html` is The Open Group POSIX/SUS sem_wait and sem_trywait reference:
https://pubs.opengroup.org/onlinepubs/009695399/functions/sem_wait.html
SHA-256: `66cdd1d3fd3045bd25f56cdea53729a961594e89a0011e90c5bc6617b84366f6`.
Its copyright remains with The Open Group; it is not CharmOS MIT code.

Additional interface references:
- https://man7.org/linux/man-pages/man3/sem_post.3.html (increment, overflow,
  signal-handler-safe posting)
- https://www.ibm.com/docs/en/aix/7.1.0?topic=s-sem-timedwait-subroutine
  (absolute CLOCK_REALTIME timeout, immediate acquisition before timeout checks)

The native supported subset and its own storage layout are specified in
`docs/abi/libc-semaphores.md`; no Linux/glibc semaphore layout is imported.

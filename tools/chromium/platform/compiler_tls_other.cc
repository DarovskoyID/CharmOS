extern thread_local int native_tls_scalar;
int IncrementNativeTlsFromAnotherUnit() { return ++native_tls_scalar; }

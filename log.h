#ifndef __LOG_C
#define __LOG_C

static const char *hexdigit_upper = "0123456789ABCDEF";
static char *tohex(char *dstHex, size_t dstStrLen, const uint8_t *srcBinary)
{
  char *p;
  size_t i;
  for (p = dstHex, i = 0; i < dstStrLen; ++i)
    *p++ = (i & 1) ? hexdigit_upper[*srcBinary++ & 0xf] : hexdigit_upper[*srcBinary >> 4];
  *p = '\0';
  return dstHex;
}

#define alloca_tohex(SRC, LEN) tohex(alloca(LEN*2+1), LEN*2, SRC) 
#define alloca_sync_key(K) alloca_tohex((K)->key, KEY_LEN)

#define LOG(X) printf(__FILE__ " (%u) %s\n", __LINE__ , X)
#define LOGF(X, ...) printf(__FILE__ " (%u) " X "\n", __LINE__ , ##__VA_ARGS__)

#endif
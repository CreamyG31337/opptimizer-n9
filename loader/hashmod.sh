#!/bin/sh -e
KMOD=../symsearch/symsearch.ko
HASH1=`cat $KMOD | openssl dgst -binary -sha1 | hexdump -e '1/1 "0x%x,"'`
KMOD=../opptimizer/opptimizer.ko
HASH2=`cat $KMOD | openssl dgst -binary -sha1 | hexdump -e '1/1 "0x%x,"'`
cat - > modhash.inc <<EOF
static const char OPP_HASH_SYMSEARCH[] = { $HASH1 };
static const char OPP_HASH_OPPTIMIZER[] = { $HASH2 };
EOF
exit 0

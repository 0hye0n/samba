#!/bin/sh

if [ $# -lt 4 ]; then
cat <<EOF
Usage: test_echo.sh SERVER USERNAME PASSWORD DOMAIN
EOF
exit 1;
fi

server="$1"
username="$2"
password="$3"
domain="$4"
shift 4

testit() {
   trap "rm -f test.$$" EXIT
   cmdline="$*"
   if ! $cmdline > test.$$ 2>&1; then
       cat test.$$;
       rm -f test.$$;
       echo "TEST FAILED - $cmdline";
       exit 1;
   fi
   rm -f test.$$;
}

transports="ncacn_np ncacn_ip_tcp"
if [ $server = "localhost" ]; then 
    transports="ncalrpc $transports"
fi

for transport in $transports; do
 for bindoptions in connect sign seal sign,seal validate padcheck bigendian bigendian,seal; do
  for ntlmoptions in \
        "--option=socket:testnonblock=True" \
        "--option=ntlmssp_client:ntlm2=yes" \
        "--option=ntlmssp_client:ntlm2=no" \
        "--option=ntlmssp_client:ntlm2=yes --option=ntlmssp_client:128bit=no" \
        "--option=ntlmssp_client:ntlm2=no  --option=ntlmssp_client:128bit=no" \
        "--option=ntlmssp_client:ntlm2=yes --option=ntlmssp_client:keyexchange=no" \
        "--option=ntlmssp_client:ntlm2=no  --option=ntlmssp_client:keyexchange=no" \
    ; do
   echo Testing $transport with $bindoptions and $ntlmoptions
   testit bin/smbtorture $transport:"$server[$bindoptions]" $ntlmoptions -U"$username"%"$password" -W $domain RPC-ECHO "$*"
  done
 done
done

# separately test the print option - its v slow
echo Testing print option
testit bin/smbtorture ncacn_np:"$server[print]" -U"$username"%"$password" -W $domain RPC-ECHO "$*"

echo "ALL OK";

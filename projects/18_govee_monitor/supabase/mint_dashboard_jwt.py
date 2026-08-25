"""Mint a long-lived JWT for the dashboard_reader role.

The SPA needs a credential that reads `reading_5m` and nothing else. It cannot
use the publishable key: every publishable key maps to `anon`, so the dashboard
would share a credential with the firmware and rotating one would mean
reflashing the other.

PostgREST reads the `role` claim and SET ROLEs to it, so a JWT signed with the
project's JWT secret is all that is required. Stdlib only — no PyJWT.

    SUPABASE_JWT_SECRET=... python3 mint_dashboard_jwt.py [--years N]

The secret is in the Supabase dashboard under Project Settings -> API ->
JWT Settings (legacy JWT secret). It signs credentials for every role including
service_role, so it must never leave .env and must never reach the SPA — only
the minted token does.
"""
import argparse
import base64
import hashlib
import hmac
import json
import os
import sys
import time

ROLE = "dashboard_reader"


def b64(raw: bytes) -> str:
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--years", type=float, default=5,
                    help="token lifetime; it is baked into a build, so long "
                         "by design (default 5)")
    ap.add_argument("--role", default=ROLE)
    args = ap.parse_args()

    secret = os.environ.get("SUPABASE_JWT_SECRET")
    if not secret:
        sys.exit("SUPABASE_JWT_SECRET is not set.\n"
                 "Supabase dashboard -> Project Settings -> API -> JWT Settings")

    now = int(time.time())
    header = {"alg": "HS256", "typ": "JWT"}
    payload = {
        "role": args.role,
        "iss": "supabase",
        "iat": now,
        "exp": now + int(args.years * 365 * 24 * 3600),
    }

    signing_input = f"{b64(json.dumps(header, separators=(',', ':')).encode())}." \
                    f"{b64(json.dumps(payload, separators=(',', ':')).encode())}"
    sig = hmac.new(secret.encode(), signing_input.encode(), hashlib.sha256).digest()
    token = f"{signing_input}.{b64(sig)}"

    print(token)
    print(f"\nrole={args.role}  expires={time.strftime('%Y-%m-%d', time.gmtime(payload['exp']))}",
          file=sys.stderr)
    print("This token is public once it ships in the SPA. It can read "
          "reading_5m and nothing else — verify with supabase/verify.sh.",
          file=sys.stderr)


if __name__ == "__main__":
    main()

import { createHmac, timingSafeEqual } from "node:crypto";

export function verifyHmac(raw: string, header: string, secret: string): boolean {
  const expected =
    "sha256=" + createHmac("sha256", secret).update(raw).digest("hex");
  const a = Buffer.from(header);
  const b = Buffer.from(expected);
  return a.length === b.length && timingSafeEqual(a, b);
}

export const DEFAULT_ANIMATION = "fireworks";

// Parse "Alice Smith=dance|Bob Jones=fireworks|*=rainbow" into a case-insensitive map.
export function parseAnimationMap(raw: string): Map<string, string> {
  const m = new Map<string, string>();
  for (const entry of raw.split("|")) {
    const [name, anim] = entry.split("=", 2).map((s) => s?.trim());
    if (name && anim) m.set(name.toLowerCase(), anim);
  }
  return m;
}

export function pickAnimation(
  map: Map<string, string>,
  developer: string | undefined,
): string {
  if (developer) {
    const hit = map.get(developer.toLowerCase());
    if (hit) return hit;
  }
  return map.get("*") ?? DEFAULT_ANIMATION;
}

// Explicit lookup with no wildcard/default fallback. Used for the tester's
// avatar: we only add a second avatar to the celebration when we actually
// have a mapping for that person (otherwise the "*" fallback would append a
// non-avatar animation like fireworks as the second slot).
export function lookupAnimation(
  map: Map<string, string>,
  person: string | undefined,
): string | undefined {
  if (!person) return undefined;
  return map.get(person.toLowerCase());
}

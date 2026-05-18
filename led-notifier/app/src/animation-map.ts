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

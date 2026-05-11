import { config } from "./config.ts";
import {
  buildJql,
  extractDeveloper,
  fetchMatchingIssues,
  listAllFields,
} from "./jira.ts";
import { postTrigger } from "./notifier.ts";

const seenIssues = new Set<string>();
let inFlight = false;

async function tick(): Promise<void> {
  if (inFlight) {
    console.warn("[tick] previous tick still running, skipping");
    return;
  }
  inFlight = true;
  try {
    const jql = buildJql();
    console.log(`[tick] jql: ${jql}`);

    const issues = await fetchMatchingIssues(jql);
    const currentKeys = new Set(issues.map((i) => i.key));

    for (const key of seenIssues) {
      if (!currentKeys.has(key)) {
        seenIssues.delete(key);
        console.log(`[tick] evicted ${key} (no longer matches)`);
      }
    }

    const newIssues = issues.filter((i) => !seenIssues.has(i.key));
    if (newIssues.length === 0) {
      console.log(`[tick] ${issues.length} matching, 0 new`);
      return;
    }

    for (const issue of newIssues) {
      const ok = await postTrigger(issue);
      if (ok) {
        seenIssues.add(issue.key);
        console.log(`[tick] celebrated ${issue.key}`);
      }
    }
  } finally {
    inFlight = false;
  }
}

export async function runLoop(): Promise<void> {
  await tick();
  setInterval(tick, config.pollIntervalMs);
}

export async function runOnce(): Promise<void> {
  await tick();
}

export async function queryCmd(jql: string): Promise<void> {
  console.log(`[query] jql: ${jql}`);
  const issues = await fetchMatchingIssues(jql);
  console.log(`[query] ${issues.length} result(s):`);
  for (const i of issues) {
    const status = i.fields.status?.name ?? "?";
    const summary = i.fields.summary ?? "";
    console.log(`  - ${i.key.padEnd(10)} [${status}] ${summary}`);
  }
}

export async function listFieldsCmd(filter: string): Promise<void> {
  const fields = await listAllFields();
  const needle = filter.toLowerCase();
  console.log(`[fields] filter: "${needle}"`);
  for (const f of fields) {
    if (f.name.toLowerCase().includes(needle)) {
      console.log(`  ${f.id.padEnd(28)} | ${f.name}`);
    }
  }
}

export async function listDevelopersCmd(): Promise<void> {
  const { developerField, projectKey, jqlExtra } = config.jira;
  if (!developerField) {
    console.error(
      "[fatal] JIRA_DEVELOPER_FIELD not set. Discover it first with --list-fields.",
    );
    process.exit(1);
  }
  const clauses: string[] = [`"${developerField}" is not EMPTY`];
  if (projectKey) clauses.push(`project = "${projectKey}"`);
  if (jqlExtra?.trim()) clauses.push(jqlExtra.trim());
  const jql = clauses.join(" AND ") + " ORDER BY updated DESC";

  console.log(`[list-devs] jql: ${jql}`);
  const issues = await fetchMatchingIssues(jql);
  const counts = new Map<string, number>();
  for (const issue of issues) {
    const name = extractDeveloper(issue);
    if (name) counts.set(name, (counts.get(name) ?? 0) + 1);
  }
  const sorted = [...counts.entries()].sort((a, b) => b[1] - a[1]);
  console.log(
    `[list-devs] ${sorted.length} unique developer(s) across ${issues.length} ticket(s):\n`,
  );
  for (const [name, n] of sorted) {
    console.log(`  ${String(n).padStart(4)}  ${name}`);
  }
  if (sorted.length > 0) {
    console.log("\n[list-devs] paste-ready env line (defaults to rainbow):");
    const entries = sorted.map(([n]) => `${n}=rainbow`).join("|");
    console.log(`DEVELOPER_ANIMATION_MAP=${entries}|*=rainbow`);
  }
}

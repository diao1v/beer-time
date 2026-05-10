const JIRA_BASE_URL = requireEnv("JIRA_BASE_URL");
const JIRA_EMAIL = requireEnv("JIRA_EMAIL");
const JIRA_API_TOKEN = requireEnv("JIRA_API_TOKEN");
const JIRA_PROJECT_KEY = process.env.JIRA_PROJECT_KEY;
const JIRA_TARGET_STATUS = process.env.JIRA_TARGET_STATUS ?? "Test OK";
const JIRA_ASSIGNEE = process.env.JIRA_ASSIGNEE ?? "me";
const JIRA_JQL_EXTRA = process.env.JIRA_JQL_EXTRA;
const LED_NOTIFIER_URL =
  process.env.LED_NOTIFIER_URL ?? "http://localhost:3000/trigger";
const POLL_INTERVAL_MS = Number(process.env.POLL_INTERVAL_MS ?? 30_000);
const JIRA_UPDATED_WINDOW = process.env.JIRA_UPDATED_WINDOW ?? "-2m";
const JIRA_PAGE_SIZE = Number(process.env.JIRA_PAGE_SIZE ?? 100);
const JIRA_MAX_TOTAL = Number(process.env.JIRA_MAX_TOTAL ?? 500);

const ONCE = process.argv.includes("--once");
const QUERY_FLAG_IDX = process.argv.indexOf("--query");
const QUERY_JQL =
  QUERY_FLAG_IDX !== -1 ? process.argv[QUERY_FLAG_IDX + 1] : undefined;

const authHeader =
  "Basic " + Buffer.from(`${JIRA_EMAIL}:${JIRA_API_TOKEN}`).toString("base64");

const seenIssues = new Set<string>();
let inFlight = false;

function requireEnv(name: string): string {
  const v = process.env[name];
  if (!v) {
    console.error(`[fatal] missing required env: ${name}`);
    process.exit(1);
  }
  return v;
}

function buildJql(): string {
  const clauses: string[] = [
    `status = "${JIRA_TARGET_STATUS}"`,
    `updated >= "${JIRA_UPDATED_WINDOW}"`,
  ];
  if (JIRA_PROJECT_KEY) clauses.push(`project = "${JIRA_PROJECT_KEY}"`);

  const a = JIRA_ASSIGNEE.trim();
  if (a && a !== "all") {
    if (a === "me") clauses.push(`assignee = currentUser()`);
    else if (a === "any") clauses.push(`assignee is not EMPTY`);
    else clauses.push(`assignee = "${a}"`);
  }

  if (JIRA_JQL_EXTRA?.trim()) clauses.push(JIRA_JQL_EXTRA.trim());
  return clauses.join(" AND ") + " ORDER BY updated DESC";
}

type JiraIssue = {
  key: string;
  fields: { summary?: string; status?: { name?: string } };
};

async function fetchMatchingIssues(jql: string): Promise<JiraIssue[]> {
  const all: JiraIssue[] = [];
  let nextPageToken: string | undefined;

  while (all.length < JIRA_MAX_TOTAL) {
    const url = new URL("/rest/api/3/search/jql", JIRA_BASE_URL);
    url.searchParams.set("jql", jql);
    url.searchParams.set("fields", "summary,status");
    url.searchParams.set("maxResults", String(JIRA_PAGE_SIZE));
    if (nextPageToken) url.searchParams.set("nextPageToken", nextPageToken);

    const res = await fetch(url, {
      headers: { authorization: authHeader, accept: "application/json" },
    });

    if (!res.ok) {
      const body = await res.text().catch(() => "");
      console.error(`[jira] ${res.status} ${res.statusText}: ${body.slice(0, 300)}`);
      return all;
    }

    const data = (await res.json()) as {
      issues?: JiraIssue[];
      nextPageToken?: string;
      isLast?: boolean;
    };
    all.push(...(data.issues ?? []));

    if (data.isLast || !data.nextPageToken) break;
    nextPageToken = data.nextPageToken;
  }

  if (all.length >= JIRA_MAX_TOTAL) {
    console.warn(
      `[jira] hit JIRA_MAX_TOTAL=${JIRA_MAX_TOTAL} cap; results may be truncated`,
    );
  }
  return all;
}

async function postTrigger(issue: JiraIssue): Promise<boolean> {
  const status = issue.fields.status?.name ?? JIRA_TARGET_STATUS;
  const payload = {
    event: "celebrate",
    source: "jira-poller",
    message: `${issue.key} → ${status}`,
  };

  try {
    const res = await fetch(LED_NOTIFIER_URL, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(payload),
    });
    if (!res.ok) {
      const body = await res.text().catch(() => "");
      console.error(
        `[notifier] ${res.status} for ${issue.key}: ${body.slice(0, 200)}`,
      );
      return false;
    }
    return true;
  } catch (err) {
    console.error(`[notifier] network error for ${issue.key}:`, (err as Error).message);
    return false;
  }
}

async function tick() {
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

console.log(
  `[boot] jira-poller → ${LED_NOTIFIER_URL} ` +
    `(status="${JIRA_TARGET_STATUS}", assignee=${JIRA_ASSIGNEE}, ` +
    `interval=${POLL_INTERVAL_MS}ms${ONCE ? ", once" : ""})`,
);

if (QUERY_JQL) {
  console.log(`[query] jql: ${QUERY_JQL}`);
  const issues = await fetchMatchingIssues(QUERY_JQL);
  console.log(`[query] ${issues.length} result(s):`);
  for (const i of issues) {
    const status = i.fields.status?.name ?? "?";
    const summary = i.fields.summary ?? "";
    console.log(`  - ${i.key.padEnd(10)} [${status}] ${summary}`);
  }
  process.exit(0);
} else if (ONCE) {
  await tick();
  process.exit(0);
} else {
  await tick();
  setInterval(tick, POLL_INTERVAL_MS);
}

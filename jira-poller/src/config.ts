function requireEnv(name: string): string {
  const v = process.env[name];
  if (!v) {
    console.error(`[fatal] missing required env: ${name}`);
    process.exit(1);
  }
  return v;
}

function flagValue(name: string): string | undefined {
  const idx = process.argv.indexOf(name);
  if (idx === -1) return undefined;
  const next = process.argv[idx + 1];
  return next && !next.startsWith("--") ? next : undefined;
}

export type CliMode =
  | { kind: "loop" }
  | { kind: "once" }
  | { kind: "query"; jql: string }
  | { kind: "list-fields"; filter: string }
  | { kind: "list-developers" };

function pickMode(): CliMode {
  if (process.argv.includes("--list-fields")) {
    return { kind: "list-fields", filter: flagValue("--list-fields") ?? "developer" };
  }
  if (process.argv.includes("--list-developers")) return { kind: "list-developers" };
  const queryJql = flagValue("--query");
  if (queryJql) return { kind: "query", jql: queryJql };
  if (process.argv.includes("--once")) return { kind: "once" };
  return { kind: "loop" };
}

export const config = {
  jira: {
    baseUrl: requireEnv("JIRA_BASE_URL"),
    email: requireEnv("JIRA_EMAIL"),
    apiToken: requireEnv("JIRA_API_TOKEN"),
    projectKey: process.env.JIRA_PROJECT_KEY,
    targetStatus: process.env.JIRA_TARGET_STATUS ?? "Test OK",
    assignee: process.env.JIRA_ASSIGNEE ?? "me",
    jqlExtra: process.env.JIRA_JQL_EXTRA,
    updatedWindow: process.env.JIRA_UPDATED_WINDOW ?? "-2m",
    pageSize: Number(process.env.JIRA_PAGE_SIZE ?? 100),
    maxTotal: Number(process.env.JIRA_MAX_TOTAL ?? 500),
    developerField: process.env.JIRA_DEVELOPER_FIELD,
  },
  notifier: {
    url: process.env.LED_NOTIFIER_URL ?? "http://localhost:3000/trigger",
  },
  pollIntervalMs: Number(process.env.POLL_INTERVAL_MS ?? 30_000),
  cli: pickMode(),
} as const;

export const authHeader =
  "Basic " + Buffer.from(`${config.jira.email}:${config.jira.apiToken}`).toString("base64");
